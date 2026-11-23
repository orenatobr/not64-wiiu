#!/bin/sh
# Regenerates the GX2 renderer shaders:
#   gen_shaders.py  ->  wiiu/source/gx2gx/shaders/asm/*.asm      (Latte assembly, committed)
#   latte-assembler ->  GFD (.gsh) in a temp dir                  (decaf-emu, pinned, GPL-3.0, build time only)
#   gfd2h.py        ->  wiiu/source/gx2gx/Gx2ShaderData.h         (uint32 arrays, committed)
# Normal CI builds use the committed header and never run this script.
#
# Requires docker and the image from docker/Dockerfile.shaders:
#   docker build -f docker/Dockerfile.shaders -t not64-shaders docker
set -e
cd "$(dirname "$0")/.."
IMG=${SHADER_IMAGE:-not64-shaders}
GEN=wiiu/source/gx2gx/shaders
ASM=$GEN/asm
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

if ! docker image inspect "$IMG" >/dev/null 2>&1; then
	docker build -f docker/Dockerfile.shaders -t "$IMG" docker
fi

python3 $GEN/gen_shaders.py lint
python3 $GEN/gen_shaders.py gen $ASM

SPECS=""
for n in 1 2 3 4 5 6 7; do
	docker run --rm -v "$PWD/$ASM":/work/asm:ro -v "$TMP":/out -w /work "$IMG" \
		--vsh asm/vs_gx.asm --psh asm/ps_tev$n.asm assemble /out/tev$n.gsh
	SPECS="$SPECS Tev$n=$TMP/tev$n.gsh"
done
python3 tools/gfd2h.py wiiu/source/gx2gx/Gx2ShaderData.h $SPECS
echo "wrote wiiu/source/gx2gx/Gx2ShaderData.h"
