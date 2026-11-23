#!/bin/sh
# Cross-checks the packed TEV uniforms + generated shader against the C++ reference:
# the unit-test binary dumps random TEV states with the reference result, a Python simulator
# executes the generated ALU groups on the packed uniforms and compares.
# Usage: tools/check-shader-sim.sh <path to test_gx2gx binary>
set -e
cd "$(dirname "$0")/.."
BIN=$1
V=$(mktemp)
trap 'rm -f "$V"' EXIT
"$BIN" "$V"
python3 wiiu/source/gx2gx/shaders/gen_shaders.py lint
python3 wiiu/source/gx2gx/shaders/gen_shaders.py sim "$V"
