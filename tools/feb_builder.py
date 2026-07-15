#!/usr/bin/env python3
"""FEB (FNA3D Effect Binary) Builder

Reads a JSON manifest describing an effect, compiles HLSL source to SPIR-V
via DXC, and assembles a .feb binary matching the format parsed by
FNA3D_LoadEffect() in FNA3D_HLSL/src/FNA3D_Effect.c.
"""

import json
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

# Parameter type enum (must match FNA3D_EffectParamType in FNA3D_Effect.h)
PARAM_TYPES = {
    "FLOAT": 0,
    "FLOAT2": 1,
    "FLOAT3": 2,
    "FLOAT4": 3,
    "INT": 4,
    "BOOL": 5,
    "MATRIX": 6,
    "TEXTURE": 7,
    "TEXTURE1D": 8,
    "TEXTURE2D": 9,
    "TEXTURE3D": 10,
    "TEXTURECUBE": 11,
}

# Shader stage enum (must match FNA3D_ShaderStage)
SHADER_STAGE_VERTEX = 0
SHADER_STAGE_PIXEL = 1

FEB_MAGIC = 0x42414E46  # "FNAB"
FEB_VERSION = 1
HEADER_SIZE = 64  # 16 * uint32


def scan_hlsl_registers(source_text: str) -> tuple[set[int], set[int]]:
    """Collect texture (tN) and sampler (sN) register indices from HLSL source.

    Used to emit -fvk-bind-register flags: DXC requires that once any
    -fvk-bind-register is passed, EVERY register used by the entry point is
    covered, so this scan must see all declarations. Extra flags for
    registers unused by the entry point are harmless.
    """
    # Strip comments so commented-out declarations don't count
    text = re.sub(r"//[^\n]*", "", source_text)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)

    # Resource arrays span multiple registers but produce a single binding;
    # reflection would undercount them. None are used today.
    if re.search(r"\[\s*\d+\s*\]\s*:\s*register\(\s*[ts]", text):
        print("WARNING: texture/sampler array detected; reflected sampler "
              "count may be wrong", file=sys.stderr)

    t_indices: set[int] = set()
    s_indices: set[int] = set()
    for kind, idx in re.findall(r"register\(\s*([ts])(\d+)", text):
        (t_indices if kind == "t" else s_indices).add(int(idx))
    return t_indices, s_indices


def compile_hlsl_to_spirv(source_path: str, entry_point: str, stage: str) -> bytes:
    """Compile an HLSL source file to SPIR-V using DXC."""
    if stage == "vertex":
        profile = "vs_6_0"
    elif stage == "pixel":
        profile = "ps_6_0"
    else:
        raise ValueError(f"Unknown shader stage: {stage}")

    output_path = source_path + ".spv"

    # SDL_GPU expects specific descriptor sets for uniform buffers:
    #   Vertex shaders:   UBOs at DescriptorSet 1 (samplers at Set 0)
    #   Fragment shaders: UBOs at DescriptorSet 3 (samplers at Set 2)
    # DXC defaults $Globals to DescriptorSet 0, so we shift it to the
    # correct set for each stage.
    if stage == "vertex":
        fvk_globals_set = "1"
    else:  # pixel
        fvk_globals_set = "3"

    # SDL_GPU fixed descriptor set layout for texture/sampler bindings:
    #   Vertex shader:  Samplers at DescriptorSet 0 (matches DXC default)
    #   Pixel shader:   Samplers at DescriptorSet 2 (must override DXC default)
    sampler_set = "0" if stage == "vertex" else "2"

    cmd = [
        "dxc",
        "-spirv",
        f"-T", profile,
        f"-E", entry_point,
        source_path,
        "-Fo", output_path,
        "-fvk-bind-globals", "0", fvk_globals_set,
    ]

    # Map texture/sampler registers to the correct descriptor set.
    # -fvk-bind-register <type-number> <space> <binding> <set>
    # space=0 is the default register space in HLSL.
    # Each HLSL register(tN)/register(sN) pair is a combined image sampler
    # at Set<s> Binding<N> in the generated SPIR-V.
    # Registers are discovered by scanning the source; both the tN and sN
    # flag are emitted per index in case a texture lacks a matching sampler
    # at that slot (or vice versa).
    with open(source_path, "r") as f:
        t_indices, s_indices = scan_hlsl_registers(f.read())
    for i in sorted(t_indices | s_indices):
        cmd.extend(["-fvk-bind-register", f"t{i}", "0", str(i), sampler_set])
        cmd.extend(["-fvk-bind-register", f"s{i}", "0", str(i), sampler_set])

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"DXC compilation failed for {source_path}:{entry_point}", file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        sys.exit(1)

    # Print warnings if any
    if result.stderr:
        print(f"DXC [{source_path}:{entry_point}]: {result.stderr.strip()}")

    with open(output_path, "rb") as f:
        spirv = f.read()

    # Clean up temp file
    os.remove(output_path)

    return spirv


# SPIR-V constants used by reflect_spirv_counts
SPIRV_MAGIC = 0x07230203
OP_DECORATE = 71
OP_VARIABLE = 59
DECORATION_BINDING = 33
DECORATION_DESCRIPTOR_SET = 34
STORAGE_CLASS_UNIFORM_CONSTANT = 0  # textures/samplers
STORAGE_CLASS_UNIFORM = 2           # uniform buffers ($Globals)


def reflect_spirv_counts(spirv: bytes, stage: str) -> tuple[int, int]:
    """Reflect (samplers, uniforms) counts from a SPIR-V binary.

    Counts only what survives DXC's dead-code elimination, which is exactly
    what SDL_CreateGPUShader needs in num_samplers/num_uniform_buffers.
    Texture (tN) and sampler (sN) share Binding N, so sampler slots are
    counted as max(binding)+1 in the stage's sampler descriptor set.
    """
    if len(spirv) < 20 or len(spirv) % 4 != 0:
        raise ValueError("Invalid SPIR-V binary size")
    words = struct.unpack(f"<{len(spirv) // 4}I", spirv)
    if words[0] != SPIRV_MAGIC:
        raise ValueError("Invalid SPIR-V magic")

    # SDL_GPU fixed descriptor set convention (see compile_hlsl_to_spirv)
    sampler_set, globals_set = (0, 1) if stage == "vertex" else (2, 3)

    decorations: dict[int, dict[int, int]] = {}  # id -> {decoration: value}
    variables: list[tuple[int, int]] = []        # (result_id, storage_class)

    pos = 5  # skip 5-word header
    while pos < len(words):
        word_count = words[pos] >> 16
        opcode = words[pos] & 0xFFFF
        if word_count == 0:
            raise ValueError(f"Malformed SPIR-V instruction at word {pos}")
        if opcode == OP_DECORATE and word_count >= 4:
            target, decoration, value = words[pos + 1], words[pos + 2], words[pos + 3]
            if decoration in (DECORATION_BINDING, DECORATION_DESCRIPTOR_SET):
                decorations.setdefault(target, {})[decoration] = value
        elif opcode == OP_VARIABLE and word_count >= 4:
            variables.append((words[pos + 2], words[pos + 3]))
        pos += word_count

    sampler_bindings: set[int] = set()
    uniform_bindings: set[tuple[int, int]] = set()
    for result_id, storage_class in variables:
        deco = decorations.get(result_id)
        if deco is None or DECORATION_DESCRIPTOR_SET not in deco:
            continue  # not a resource (Input/Output/Function/etc.)
        dset = deco[DECORATION_DESCRIPTOR_SET]
        binding = deco.get(DECORATION_BINDING, 0)
        if storage_class == STORAGE_CLASS_UNIFORM_CONSTANT:
            if dset != sampler_set:
                raise ValueError(
                    f"Texture/sampler at DescriptorSet {dset}, expected "
                    f"{sampler_set} for {stage} stage (missing "
                    f"-fvk-bind-register?)")
            sampler_bindings.add(binding)
        elif storage_class == STORAGE_CLASS_UNIFORM:
            if dset != globals_set:
                raise ValueError(
                    f"Uniform buffer at DescriptorSet {dset}, expected "
                    f"{globals_set} for {stage} stage")
            uniform_bindings.add((dset, binding))

    samplers = max(sampler_bindings) + 1 if sampler_bindings else 0
    if sampler_bindings and sampler_bindings != set(range(samplers)):
        print(f"WARNING: sparse sampler bindings {sorted(sampler_bindings)}; "
              f"runtime binds slots 0..{samplers - 1} contiguously",
              file=sys.stderr)
    uniforms = len(uniform_bindings)

    return samplers, uniforms


class StringTable:
    """Builds a concatenated null-terminated string table and tracks offsets."""

    def __init__(self):
        self.strings: list[bytes] = []
        self.offsets: dict[str, int] = {}

    def add(self, s: str) -> int:
        """Add a string, return its offset. Deduplicates."""
        if s in self.offsets:
            return self.offsets[s]
        offset = sum(len(b) for b in self.strings)
        self.strings.append(s.encode("utf-8") + b"\0")
        self.offsets[s] = offset
        return offset

    def data(self) -> bytes:
        return b"".join(self.strings)

    def size(self) -> int:
        return sum(len(b) for b in self.strings)


def pack_default_value(param_type: str, default) -> bytes:
    """Pack a default value into 64 bytes (16 floats in row-major).

    The FNA3D_EffectParam.defaultValue is a union of:
      float floatValues[16];  // float through matrix
      int32_t intValues[16];
      uint32_t boolValue;
    We always write 64 bytes (16 x float).
    """
    values = [0.0] * 16

    if param_type == "MATRIX":
        # Matrix: 16 floats, row-major
        if default is not None:
            for i in range(min(16, len(default))):
                values[i] = float(default[i])
    elif param_type in ("FLOAT", "FLOAT2", "FLOAT3", "FLOAT4"):
        count = {"FLOAT": 1, "FLOAT2": 2, "FLOAT3": 3, "FLOAT4": 4}[param_type]
        if default is not None:
            for i in range(min(count, len(default))):
                values[i] = float(default[i])
    elif param_type in ("INT", "BOOL"):
        if default is not None:
            values[0] = float(int(default[0] if isinstance(default, list) else default))
    # TEXTURE types have no meaningful default value

    return struct.pack("<16f", *values)


def build_feb(manifest: dict, manifest_dir: str) -> bytes:
    """Build a complete FEB binary from a manifest dict."""

    strings = StringTable()
    techniques = manifest["techniques"]
    params = manifest.get("parameters", [])

    # --- Compile shaders ---
    # samplers/uniforms counts are reflected from the compiled SPIR-V; any
    # leftover manual counts in the manifest are only checked for mismatch.
    def check_manifest_counts(shader_json, stage_name, samplers, uniforms):
        for key, reflected in (("samplers", samplers), ("uniforms", uniforms)):
            manual = shader_json.get(key)
            if manual is not None and manual != reflected:
                print(f"WARNING: {shader_json['source']}:{shader_json['entry']} "
                      f"({stage_name}) manifest says {key}={manual}, "
                      f"reflection says {reflected}; using {reflected}",
                      file=sys.stderr)

    shader_entries = []  # list of (stage, entry_name, spirv_bytes, samplers, uniforms)
    for tech in techniques:
        for p in tech["passes"]:
            vs = p.get("vertexShader")
            ps = p.get("pixelShader")
            if vs:
                src = os.path.join(manifest_dir, vs["source"])
                spirv = compile_hlsl_to_spirv(src, vs["entry"], "vertex")
                samplers, uniforms = reflect_spirv_counts(spirv, "vertex")
                check_manifest_counts(vs, "vertex", samplers, uniforms)
                shader_entries.append((SHADER_STAGE_VERTEX, vs["entry"], spirv,
                    samplers, uniforms))
            if ps:
                src = os.path.join(manifest_dir, ps["source"])
                spirv = compile_hlsl_to_spirv(src, ps["entry"], "pixel")
                samplers, uniforms = reflect_spirv_counts(spirv, "pixel")
                check_manifest_counts(ps, "pixel", samplers, uniforms)
                shader_entries.append((SHADER_STAGE_PIXEL, ps["entry"], spirv,
                    samplers, uniforms))

    # --- Compute counts ---
    technique_count = len(techniques)
    pass_count = sum(len(t["passes"]) for t in techniques)
    param_count = len(params)
    shader_count = len(shader_entries)

    # --- Build string table ---
    # We need to add all names first to get offsets
    for tech in techniques:
        strings.add(tech["name"])
        for p in tech["passes"]:
            strings.add(p["name"])
    for param in params:
        strings.add(param["name"])
        strings.add(param.get("semantic", ""))
    for stage, entry, _, _, _ in shader_entries:
        strings.add(entry)

    string_table_data = strings.data()
    string_table_size = strings.size()

    # --- Compute section offsets ---
    # Layout: [Header 64] [StringTable] [Parameters] [Techniques] [Passes] [Shaders] [SPIR-V]
    offset = HEADER_SIZE
    offset += string_table_size

    param_offset = offset
    # Each parameter entry: nameOff(4) + semOff(4) + type(1) + pad(3) +
    #   registerIndex(4) + defaultValue(64) + annotationCount(4) = 84 bytes + annotations
    # NOTE: The current FNA3D_LoadEffect parser seeks with stride 32 per parameter,
    # which is a BUG. We produce correct sequential layout here. When paramCount=0,
    # the bug does not trigger.
    param_entry_sizes = []
    for param in params:
        size = 84  # fixed portion
        size += param.get("annotations", []).__len__() * 40
        param_entry_sizes.append(size)
    param_section_size = sum(param_entry_sizes)

    offset += param_section_size
    technique_offset = offset
    # Each technique: nameOff(4) + passStart(4) + passCount(4) + annotationCount(4) = 16 bytes
    # Annotations are NOT skipped by the current parser (bug), so we keep annotationCount=0
    technique_section_size = technique_count * 16
    # NOTE: technique annotations are currently not stored inline

    offset += technique_section_size
    pass_offset = offset
    # Each pass: nameOff(4) + vsIdx(4) + psIdx(4) + rsCount(4) + ssCount(4) + reserved(4) = 24
    pass_section_size = pass_count * 24

    offset += pass_section_size
    shader_offset = offset
    # Each shader: stage(1) + pad(3) + entryOff(4) + spirvOff(4) + spirvSize(4) + samplers(4) + uniforms(4) = 24
    shader_section_size = shader_count * 24

    offset += shader_section_size
    spirv_offset = offset

    # Compute total SPIR-V data size
    spirv_data_parts = []
    spirv_offsets = []
    current_spirv_off = 0
    for _, _, spirv, _, _ in shader_entries:
        # Ensure 4-byte alignment
        spirv_data_parts.append(spirv)
        spirv_offsets.append(current_spirv_off)
        current_spirv_off += len(spirv)

    # --- Write body first (everything after the header) ---
    body = bytearray()

    # String table
    body.extend(string_table_data)

    # Parameters
    for param in params:
        name_off = strings.offsets[param["name"]]
        sem_off = strings.offsets[param.get("semantic", "")]
        ptype = PARAM_TYPES[param["type"]]
        reg = param.get("register", 0)
        default_val = pack_default_value(param["type"], param.get("default"))

        body.extend(struct.pack("<I", name_off))
        body.extend(struct.pack("<I", sem_off))
        body.extend(struct.pack("<B", ptype))
        body.extend(b"\0\0\0")  # padding
        body.extend(struct.pack("<I", reg))
        body.extend(default_val)  # 64 bytes
        annotations = param.get("annotations", [])
        body.extend(struct.pack("<I", len(annotations)))
        for ann in annotations:
            # annotation: nameOff(4) + type(1) + pad(3) + value(32) = 40 bytes
            ann_name_off = strings.add(ann["name"])
            ann_type = PARAM_TYPES[ann["type"]]
            ann_value = pack_default_value(ann["type"], ann.get("value"))
            body.extend(struct.pack("<I", ann_name_off))
            body.extend(struct.pack("<B", ann_type))
            body.extend(b"\0\0\0")  # padding
            body.extend(ann_value[:32])  # annotation value is 32 bytes (8 floats)

    # Techniques
    pass_index = 0
    for tech in techniques:
        name_off = strings.offsets[tech["name"]]
        pass_start = pass_index
        pc = len(tech["passes"])
        body.extend(struct.pack("<I", name_off))
        body.extend(struct.pack("<I", pass_start))
        body.extend(struct.pack("<I", pc))
        body.extend(struct.pack("<I", 0))  # annotationCount = 0
        pass_index += pc

    # Passes
    # Build shader index map: (stage, entry_name) -> shader array index
    shader_index_map = {}
    for idx, (stage, entry, _, _, _) in enumerate(shader_entries):
        shader_index_map[(stage, entry)] = idx

    for tech in techniques:
        for p in tech["passes"]:
            name_off = strings.offsets[p["name"]]
            vs_idx = -1
            ps_idx = -1
            vs = p.get("vertexShader")
            ps = p.get("pixelShader")
            if vs:
                vs_idx = shader_index_map.get((SHADER_STAGE_VERTEX, vs["entry"]), -1)
            if ps:
                ps_idx = shader_index_map.get((SHADER_STAGE_PIXEL, ps["entry"]), -1)
            rs_count = len(p.get("renderStates", []))
            ss_count = len(p.get("samplerStates", []))

            body.extend(struct.pack("<I", name_off))
            body.extend(struct.pack("<i", vs_idx))
            body.extend(struct.pack("<i", ps_idx))
            body.extend(struct.pack("<I", rs_count))
            body.extend(struct.pack("<I", ss_count))
            body.extend(struct.pack("<I", 0))  # reserved

    # Shaders (24 bytes each):
    # stage(1)+pad(3)+entryOff(4)+spirvOff(4)+spirvSize(4)+samplers(4)+uniforms(4)
    for idx, (stage, entry, spirv, samplers, uniforms) in enumerate(shader_entries):
        entry_off = strings.offsets[entry]
        spirv_data_off = spirv_offsets[idx]
        spirv_data_size = len(spirv)

        body.extend(struct.pack("<B", stage))
        body.extend(b"\0\0\0")  # padding
        body.extend(struct.pack("<I", entry_off))
        body.extend(struct.pack("<I", spirv_data_off))
        body.extend(struct.pack("<I", spirv_data_size))
        body.extend(struct.pack("<I", samplers))
        body.extend(struct.pack("<I", uniforms))

    # SPIR-V data (already in-order from shader_entries)
    for _, _, spirv, _, _ in shader_entries:
        body.extend(spirv)

    # --- Compute total size from actual bytes ---
    total_size = HEADER_SIZE + len(body)

    # --- Prepend header ---
    header = struct.pack(
        "<16I",
        FEB_MAGIC,            # [0]
        FEB_VERSION,          # [1]
        technique_count,      # [2]
        pass_count,           # [3]
        param_count,          # [4]
        shader_count,         # [5]
        string_table_size,    # [6]
        param_offset,         # [7]
        technique_offset,     # [8]
        pass_offset,          # [9]
        shader_offset,        # [10]
        spirv_offset,         # [11]
        total_size,           # [12]
        0,                    # [13] reserved
        0,                    # [14] reserved
        0,                    # [15] reserved
    )
    buf = header + body

    return bytes(buf)


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <manifest.json>", file=sys.stderr)
        sys.exit(1)

    manifest_path = sys.argv[1]
    manifest_dir = os.path.dirname(os.path.abspath(manifest_path))

    with open(manifest_path, "r") as f:
        manifest = json.load(f)

    # Determine output path: strip .json (and .feb.json) → .feb
    if manifest_path.endswith(".feb.json"):
        output_path = manifest_path[:-5]  # strip .json
    elif manifest_path.endswith(".json"):
        output_path = manifest_path[:-5] + ".feb"
    else:
        output_path = manifest_path + ".feb"

    feb_data = build_feb(manifest, manifest_dir)

    with open(output_path, "wb") as f:
        f.write(feb_data)

    print(f"Built {output_path} ({len(feb_data)} bytes, "
          f"{manifest['techniques'].__len__()} techniques, "
          f"{sum(len(t['passes']) for t in manifest['techniques'])} passes)")


if __name__ == "__main__":
    main()
