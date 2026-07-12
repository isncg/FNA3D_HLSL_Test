# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

Test/render pipeline project for **FNA3D_HLSL** (sibling repo at `../FNA3D_HLSL/`). FNA3D_HLSL is a fork of FNA3D that replaces MojoShader with DXC, compiling HLSL to SPIR-V and packing it into a custom **FEB (FNA3D Effect Binary)** format. At runtime, effects are loaded via `FNA3D_CreateEffect()` and dispatched through SDL_GPU (Vulkan/Metal/D3D12).

## Build & run

```bash
# One-shot: build FEB assets + configure CMake + compile
./build_linux.sh

# Run the test
./out_linux/triangle_test

# Rebuild FEB only (after HLSL or manifest changes)
python3 tools/feb_builder.py assets/effects/triangle.feb.json

# Rebuild C code only (after .c changes)
make -C build_linux -j$(nproc)
```

The build script depends on `dxc` (DirectX Shader Compiler) being on `PATH` for HLSL → SPIR-V compilation. Output binaries land in `out_linux/`.

## Architecture

### Shader pipeline

```
HLSL source (.hlsl)
  → DXC -spirv -T vs_6_0 / -T ps_6_0
    → SPIR-V binary
      → feb_builder.py (reads .feb.json manifest)
        → .feb file (binary with header + string table + sections)
          → FNA3D_CreateEffect() at runtime
            → SDL_GPU creates shaders → bind pipeline → draw
```

### FEB binary format

Defined in `tools/feb_builder.py` and parsed by `FNA3D_HLSL/src/FNA3D_Effect.c`. Header is 64 bytes (16 × uint32 LE), magic `0x42414E46` ("FNAB"). Layout: `[Header] [StringTable] [Parameters] [Techniques] [Passes] [Shaders] [SPIR-V data]`.

The sibling FNA3D_HLSL library is built from source via `add_subdirectory()` in CMakeLists.txt. It produces `libFNA3D.so`.

### Key constraint: no uniform API

FNA3D_HLSL does not yet implement uniform/constant buffer update APIs. Shaders must be pass-through (NDC position + vertex color only) or rely on default parameter values baked into the FEB. Keep `paramCount = 0` in manifests — the parameter parser has a known stride bug (expects 32-byte entries, but actual entries are ≥84 bytes).

### DXC vertex attribute location

DXC assigns SPIR-V locations in HLSL parameter declaration order (0, 1, 2...), not by `usage*16+index`. The `FNA3D_VertexElement` declarations in C code must match the HLSL struct field order.

### COLOR byte order

`FNA3D_VERTEXELEMENTFORMAT_COLOR` uses BGRA byte order in memory (XNA convention). Vertex struct fields should be `b, g, r, a`.

## Adding a new test

1. Write HLSL shaders in `assets/shaders/` (use SM 6.0 profiles: `vs_6_0` / `ps_6_0`)
2. Write a manifest in `assets/effects/<name>.feb.json` — `source` paths are relative to the manifest file
3. Write a self-contained C test in `src/<name>_test.c` (see `src/triangle_test.c` for the template)
4. Add an `add_executable` + `target_link_libraries` block to `CMakeLists.txt`
5. Add the `python3 tools/feb_builder.py` step to `build_linux.sh`
