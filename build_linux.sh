#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== Building FEB shader assets ==="
python3 tools/feb_builder.py assets/effects/triangle.feb.json

echo "=== Configuring CMake ==="
cmake -B build_linux -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug

echo "=== Building ==="
make -C build_linux -j$(nproc)

echo "=== Done ==="
echo "Run: ./out_linux/triangle_test"
