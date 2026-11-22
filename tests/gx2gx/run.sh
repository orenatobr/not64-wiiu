#!/bin/sh
# Host-side unit tests for the GX2 renderer's pure logic. Usage: tests/gx2gx/run.sh
set -e
cd "$(dirname "$0")/../.."
SRC=wiiu/source/gx2gx
OUT=${TMPDIR:-/tmp}/not64-gx2gx-tests
mkdir -p "$OUT"
CXX=${CXX:-g++}
$CXX -std=c++17 -Wall -Wextra -Wno-unused-parameter -O1 -g -fsanitize=address,undefined \
	-I"$SRC" \
	tests/gx2gx/test_gx2gx.cpp \
	$SRC/Gx2Core.cpp $SRC/Gx2Tev.cpp $SRC/Gx2Tex.cpp $SRC/Gx2Math.cpp \
	-o "$OUT/test_gx2gx"
"$OUT/test_gx2gx"
if [ -x tools/check-shader-sim.sh ] && [ -f "$SRC/shaders/ubershader.py" ]; then
	tools/check-shader-sim.sh "$OUT/test_gx2gx"
fi
