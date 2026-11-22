/*
 * Gx2Tex.h: GX texture format handling. glN64_GX writes texture memory in the
 * GameCube tiled layout; the shim detiles it into linear RGBA8 when the texture
 * is first used (see docs/GX2_RENDERER.md, "Texture path"). Pure logic.
 */
#ifndef GX2GX_TEX_H
#define GX2GX_TEX_H

#include <stddef.h>
#include <stdint.h>

namespace gx2gx {

/* GX tile geometry for a texture format. Returns false for unsupported formats. */
bool texFormatInfo(int gxFmt, int *blockW, int *blockH, int *bytesPerBlock);

/* Size in bytes of the GX-tiled image (dimensions rounded up to whole blocks). */
size_t texTiledBytes(int gxFmt, int w, int h);

/*
 * Convert a GX tiled image to linear RGBA8 (4 bytes per texel, w*h texels, row stride w*4).
 * srcBytes bounds every read; returns false (dst untouched) when the format is unsupported,
 * dimensions are invalid, or srcBytes is smaller than the tiled size.
 */
bool texDecode(int gxFmt, int w, int h, const uint8_t *src, size_t srcBytes, uint8_t *dstRgba);

/* Per-channel expansions used by the decoder (exposed for the unit tests). */
uint32_t expand565(uint16_t v);   /* returns 0xRRGGBBAA */
uint32_t expand5A3(uint16_t v);   /* returns 0xRRGGBBAA */

} /* namespace gx2gx */

#endif
