#!/bin/sh
# Validates the latte-assembler pipeline against the compiler-generated register words
# in dborth's Texture2DShader (wiiu/source/drivers/wut/shaders/Texture2DShader.cpp).
# The program words differ (that shader comes from a real GLSL compiler); the register
# arrays (SQ_PGM_RESOURCES, SPI_*, CB_*, DB_* ...) and the CALL_FS control-flow word must be identical.
# Needs the image from docker/Dockerfile.shaders (docker build -f docker/Dockerfile.shaders -t not64-shaders docker).
set -e
cd "$(dirname "$0")/.."
IMG=${SHADER_IMAGE:-not64-shaders}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cp tests/gx2gx/validate/texture2d.vsh tests/gx2gx/validate/texture2d.psh "$TMP/"
docker run --rm -v "$TMP":/work "$IMG" --vsh texture2d.vsh --psh texture2d.psh assemble tex2d.gsh
python3 tools/gfd2h.py "$TMP/tex2d.h" Tex2D="$TMP/tex2d.gsh"
python3 tools/validate_regs.py "$TMP/tex2d.h" wiiu/source/drivers/wut/shaders/Texture2DShader.cpp
