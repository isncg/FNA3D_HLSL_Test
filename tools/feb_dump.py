#!/usr/bin/env python3
"""FEB (FNA3D Effect Binary) Dumper

Parses a .feb binary produced by feb_builder.py and prints its contents
(header, string table, parameters, techniques, passes, shaders) in
human-readable form. Optionally disassembles the embedded SPIR-V blobs
via spirv-dis or extracts them to .spv files.
"""

import os
import shutil
import struct
import subprocess
import sys

# Reverse of PARAM_TYPES in feb_builder.py (FNA3D_EffectParamType)
PARAM_TYPE_NAMES = {
    0: "FLOAT",
    1: "FLOAT2",
    2: "FLOAT3",
    3: "FLOAT4",
    4: "INT",
    5: "BOOL",
    6: "MATRIX",
    7: "TEXTURE",
    8: "TEXTURE1D",
    9: "TEXTURE2D",
    10: "TEXTURE3D",
    11: "TEXTURECUBE",
}

FLOAT_COUNTS = {"FLOAT": 1, "FLOAT2": 2, "FLOAT3": 3, "FLOAT4": 4}

# Shader stage enum (must match FNA3D_ShaderStage)
STAGE_NAMES = {0: "vertex", 1: "pixel"}
STAGE_ABBREV = {0: "vs", 1: "ps"}

FEB_MAGIC = 0x42414E46  # "FNAB"
FEB_VERSION = 1
HEADER_SIZE = 64  # 16 * uint32
SPIRV_MAGIC = 0x07230203

HEADER_FIELDS = [
    "magic", "version", "techniqueCount", "passCount",
    "paramCount", "shaderCount", "stringTableSize", "paramOffset",
    "techniqueOffset", "passOffset", "shaderOffset", "spirvOffset",
    "totalSize",
]


def read_cstr(strtab: bytes, offset: int) -> str:
    """Read a null-terminated UTF-8 string from the string table."""
    if offset < 0 or offset >= len(strtab):
        return f"<bad offset {offset}>"
    end = strtab.find(b"\0", offset)
    if end == -1:
        end = len(strtab)
    return strtab[offset:end].decode("utf-8", errors="replace")


def parse_feb(data: bytes) -> dict:
    """Parse a FEB binary into a dict of header/params/techniques/passes/shaders."""
    if len(data) < HEADER_SIZE:
        raise ValueError(f"file too small for 64-byte header ({len(data)} bytes)")

    fields = struct.unpack_from("<16I", data, 0)
    header = dict(zip(HEADER_FIELDS, fields))
    header["reserved"] = fields[13:16]

    warnings = []
    if header["magic"] != FEB_MAGIC:
        warnings.append(f"bad magic 0x{header['magic']:08X} "
                        f"(expected 0x{FEB_MAGIC:08X} 'FNAB')")
    if header["version"] > FEB_VERSION:
        warnings.append(f"version {header['version']} is newer than "
                        f"supported version {FEB_VERSION}")
    if header["totalSize"] != len(data):
        warnings.append(f"header totalSize={header['totalSize']} but file "
                        f"is {len(data)} bytes")
    if any(header["reserved"]):
        warnings.append(f"reserved header words are nonzero: {header['reserved']}")

    strtab = data[HEADER_SIZE:HEADER_SIZE + header["stringTableSize"]]

    # Parameters (variable size: 84-byte fixed portion + annotationCount * 40)
    params = []
    off = header["paramOffset"]
    for _ in range(header["paramCount"]):
        name_off, sem_off = struct.unpack_from("<II", data, off)
        (ptype,) = struct.unpack_from("<B", data, off + 8)
        (reg,) = struct.unpack_from("<I", data, off + 12)
        default = struct.unpack_from("<16f", data, off + 16)
        (ann_count,) = struct.unpack_from("<I", data, off + 80)
        off += 84
        annotations = []
        for _ in range(ann_count):
            (ann_name_off,) = struct.unpack_from("<I", data, off)
            (ann_type,) = struct.unpack_from("<B", data, off + 4)
            ann_value = struct.unpack_from("<8f", data, off + 8)
            annotations.append({
                "name": read_cstr(strtab, ann_name_off),
                "type": ann_type,
                "value": ann_value,
            })
            off += 40
        params.append({
            "name": read_cstr(strtab, name_off),
            "semantic": read_cstr(strtab, sem_off),
            "type": ptype,
            "register": reg,
            "default": default,
            "annotations": annotations,
        })

    # Techniques (16 bytes each)
    techniques = []
    off = header["techniqueOffset"]
    for _ in range(header["techniqueCount"]):
        name_off, pass_start, pass_count, ann_count = \
            struct.unpack_from("<IIII", data, off)
        techniques.append({
            "name": read_cstr(strtab, name_off),
            "passStart": pass_start,
            "passCount": pass_count,
            "annotationCount": ann_count,
        })
        off += 16

    # Passes (24 bytes each)
    passes = []
    off = header["passOffset"]
    for _ in range(header["passCount"]):
        name_off, vs_idx, ps_idx, rs_count, ss_count, reserved = \
            struct.unpack_from("<IiiIII", data, off)
        passes.append({
            "name": read_cstr(strtab, name_off),
            "vertexShaderIndex": vs_idx,
            "pixelShaderIndex": ps_idx,
            "renderStateCount": rs_count,
            "samplerStateCount": ss_count,
        })
        off += 24

    # Shaders (24 bytes each); spirvDataOffset is relative to spirvOffset
    shaders = []
    off = header["shaderOffset"]
    for _ in range(header["shaderCount"]):
        (stage,) = struct.unpack_from("<B", data, off)
        entry_off, spirv_off, spirv_size, samplers, uniforms = \
            struct.unpack_from("<IIIII", data, off + 4)
        start = header["spirvOffset"] + spirv_off
        spirv = data[start:start + spirv_size]
        if len(spirv) != spirv_size:
            warnings.append(f"shader {len(shaders)}: SPIR-V range "
                            f"[{start}, {start + spirv_size}) exceeds file size")
        shaders.append({
            "stage": stage,
            "entry": read_cstr(strtab, entry_off),
            "spirvOffset": spirv_off,
            "spirvSize": spirv_size,
            "samplers": samplers,
            "uniforms": uniforms,
            "spirv": spirv,
        })
        off += 24

    return {
        "header": header,
        "strtab": strtab,
        "params": params,
        "techniques": techniques,
        "passes": passes,
        "shaders": shaders,
        "warnings": warnings,
    }


def fmt_floats(values) -> str:
    return " ".join(f"{v:g}" for v in values)


def fmt_default(ptype: int, values) -> list[str]:
    """Format a parameter default value as printable lines, per type."""
    type_name = PARAM_TYPE_NAMES.get(ptype, "")
    if type_name == "MATRIX":
        return [fmt_floats(values[row * 4:row * 4 + 4]) for row in range(4)]
    if type_name in FLOAT_COUNTS:
        return [fmt_floats(values[:FLOAT_COUNTS[type_name]])]
    if type_name in ("INT", "BOOL"):
        return [str(int(values[0]))]
    return []  # TEXTURE types have no meaningful default value


def dump_string_table(strtab: bytes):
    strings = []
    pos = 0
    while pos < len(strtab):
        end = strtab.find(b"\0", pos)
        if end == -1:
            end = len(strtab)
        strings.append((pos, strtab[pos:end].decode("utf-8", errors="replace")))
        pos = end + 1

    print(f"=== String Table ({len(strtab)} bytes, {len(strings)} strings) ===")
    for offset, s in strings:
        print(f"[{offset:4d}] {s!r}")


def disasm_spirv(spirv: bytes) -> str:
    """Disassemble a SPIR-V binary using spirv-dis (reads stdin)."""
    result = subprocess.run(["spirv-dis"], input=spirv, capture_output=True)
    if result.returncode != 0:
        return f"<spirv-dis failed: {result.stderr.decode(errors='replace').strip()}>"
    return result.stdout.decode(errors="replace").rstrip()


def dump_feb(path: str, disasm: bool = False, extract_dir: str = None):
    with open(path, "rb") as f:
        data = f.read()
    feb = parse_feb(data)
    header = feb["header"]

    for w in feb["warnings"]:
        print(f"WARNING: {w}", file=sys.stderr)

    magic_ascii = header["magic"].to_bytes(4, "little").decode("ascii", errors="replace")
    print(f"=== FEB Header ===")
    print(f"magic: 0x{header['magic']:08X} ({magic_ascii!r})  "
          f"version: {header['version']}")
    print(f"techniques: {header['techniqueCount']}  "
          f"passes: {header['passCount']}  "
          f"params: {header['paramCount']}  "
          f"shaders: {header['shaderCount']}")
    sections = [
        ("stringTable", HEADER_SIZE, header["paramOffset"]),
        ("params", header["paramOffset"], header["techniqueOffset"]),
        ("techniques", header["techniqueOffset"], header["passOffset"]),
        ("passes", header["passOffset"], header["shaderOffset"]),
        ("shaders", header["shaderOffset"], header["spirvOffset"]),
        ("spirv", header["spirvOffset"], header["totalSize"]),
    ]
    for name, start, end in sections:
        print(f"  {name:<12} offset={start:<6} size={end - start}")
    print(f"totalSize: {header['totalSize']} (file: {len(data)} bytes)")
    print()

    dump_string_table(feb["strtab"])
    print()

    print(f"=== Parameters ({len(feb['params'])}) ===")
    for i, p in enumerate(feb["params"]):
        type_name = PARAM_TYPE_NAMES.get(p["type"], f"<unknown {p['type']}>")
        line = f"[{i}] {p['name']}  type={type_name}  register={p['register']}"
        if p["semantic"]:
            line += f"  semantic={p['semantic']}"
        print(line)
        default_lines = fmt_default(p["type"], p["default"])
        for j, dl in enumerate(default_lines):
            prefix = "    default: " if j == 0 else "             "
            print(f"{prefix}{dl}")
        for ann in p["annotations"]:
            ann_type = PARAM_TYPE_NAMES.get(ann["type"], f"<unknown {ann['type']}>")
            print(f"    annotation: {ann['name']}  type={ann_type}  "
                  f"value: {fmt_floats(ann['value'])}")
    print()

    print(f"=== Techniques ({len(feb['techniques'])}) ===")
    for i, t in enumerate(feb["techniques"]):
        line = (f"[{i}] {t['name']}  passStart={t['passStart']}  "
                f"passCount={t['passCount']}")
        if t["annotationCount"]:
            line += f"  annotations={t['annotationCount']}"
        print(line)
    print()

    print(f"=== Passes ({len(feb['passes'])}) ===")
    for i, p in enumerate(feb["passes"]):
        vs = p["vertexShaderIndex"]
        ps = p["pixelShaderIndex"]
        print(f"[{i}] {p['name']}  vs={'none' if vs < 0 else vs}  "
              f"ps={'none' if ps < 0 else ps}  "
              f"renderStates={p['renderStateCount']}  "
              f"samplerStates={p['samplerStateCount']}")
    print()

    print(f"=== Shaders ({len(feb['shaders'])}) ===")
    feb_name = os.path.basename(path)
    if feb_name.endswith(".feb"):
        feb_name = feb_name[:-4]
    for i, s in enumerate(feb["shaders"]):
        stage_name = STAGE_NAMES.get(s["stage"], f"<unknown {s['stage']}>")
        if len(s["spirv"]) >= 4:
            (spirv_magic,) = struct.unpack_from("<I", s["spirv"], 0)
            magic_str = "OK" if spirv_magic == SPIRV_MAGIC else f"BAD(0x{spirv_magic:08X})"
        else:
            magic_str = "BAD(truncated)"
        print(f"[{i}] stage={stage_name}  entry={s['entry']}  "
              f"spirv: offset={s['spirvOffset']} size={s['spirvSize']} "
              f"magic={magic_str}  samplers={s['samplers']} "
              f"uniforms={s['uniforms']}")

        if extract_dir is not None:
            os.makedirs(extract_dir, exist_ok=True)
            abbrev = STAGE_ABBREV.get(s["stage"], f"stage{s['stage']}")
            out_path = os.path.join(
                extract_dir, f"{feb_name}.{i}.{abbrev}.{s['entry']}.spv")
            with open(out_path, "wb") as f:
                f.write(s["spirv"])
            print(f"    wrote {out_path}")

        if disasm:
            if shutil.which("spirv-dis") is None:
                print("    <spirv-dis not found on PATH, skipping disassembly>")
            else:
                print("    ; SPIR-V disassembly (spirv-dis)")
                for line in disasm_spirv(s["spirv"]).splitlines():
                    print(f"    {line}")


def main():
    disasm = False
    extract_dir = None
    files = []
    args = sys.argv[1:]
    i = 0
    while i < len(args):
        arg = args[i]
        if arg == "--disasm":
            disasm = True
        elif arg == "--extract":
            i += 1
            if i >= len(args):
                print("--extract requires a directory argument", file=sys.stderr)
                sys.exit(1)
            extract_dir = args[i]
        else:
            files.append(arg)
        i += 1

    if not files:
        print(f"Usage: {sys.argv[0]} <file.feb> [more.feb ...] "
              f"[--disasm] [--extract <dir>]", file=sys.stderr)
        sys.exit(1)

    failed = False
    for idx, path in enumerate(files):
        if len(files) > 1:
            if idx > 0:
                print()
            print(f"##### {path} #####")
        try:
            dump_feb(path, disasm, extract_dir)
        except (OSError, ValueError, struct.error) as e:
            print(f"ERROR: {path}: {e}", file=sys.stderr)
            failed = True

    if failed:
        sys.exit(1)


if __name__ == "__main__":
    main()
