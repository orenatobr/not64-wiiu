/*
 * Gx2Tex.cpp: see Gx2Tex.h.
 *
 * GX texel layouts (big-endian data, as written by glN64_GX on the big-endian Espresso CPU):
 *   I4, CI4          8x8 blocks of 32 bytes, 2 texels per byte (high nibble = left texel)
 *   I8, IA4, CI8     8x4 blocks of 32 bytes, 1 byte per texel (IA4: alpha high nibble, intensity low)
 *   IA8, RGB565, RGB5A3, CI14
 *                    4x4 blocks of 32 bytes, 16 bit big-endian per texel (IA8: alpha high byte)
 *   RGBA8            4x4 blocks of 64 bytes: 32 bytes of (A,R) pairs, then 32 bytes of (G,B) pairs
 * Blocks are stored row by row; dimensions are rounded up to whole blocks.
 */
#include "Gx2Tex.h"
#include "compat/ogc/gx_defs.h"
#include <string.h>

namespace gx2gx {

bool texFormatInfo(int fmt, int *bw, int *bh, int *bytes)
{
	switch (fmt) {
	case GX_TF_I4:
	case GX_TF_CI4:
		*bw = 8; *bh = 8; *bytes = 32; return true;
	case GX_TF_I8:
	case GX_TF_IA4:
	case GX_TF_CI8:
		*bw = 8; *bh = 4; *bytes = 32; return true;
	case GX_TF_IA8:
	case GX_TF_RGB565:
	case GX_TF_RGB5A3:
	case GX_TF_CI14:
		*bw = 4; *bh = 4; *bytes = 32; return true;
	case GX_TF_RGBA8:
		*bw = 4; *bh = 4; *bytes = 64; return true;
	default:
		return false;
	}
}

size_t texTiledBytes(int fmt, int w, int h)
{
	int bw, bh, bytes;
	if (w <= 0 || h <= 0 || !texFormatInfo(fmt, &bw, &bh, &bytes))
		return 0;
	size_t nx = ((size_t) w + bw - 1) / bw;
	size_t ny = ((size_t) h + bh - 1) / bh;
	return nx * ny * (size_t) bytes;
}

static inline uint32_t rgba(uint32_t r, uint32_t g, uint32_t b, uint32_t a)
{
	return (r << 24) | (g << 16) | (b << 8) | a;
}

uint32_t expand565(uint16_t v)
{
	uint32_t r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
	return rgba((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2), 255);
}

uint32_t expand5A3(uint16_t v)
{
	if (v & 0x8000) {
		uint32_t r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
		return rgba((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2), 255);
	}
	uint32_t a = (v >> 12) & 7, r = (v >> 8) & 15, g = (v >> 4) & 15, b = v & 15;
	return rgba(r * 17, g * 17, b * 17, (a << 5) | (a << 2) | (a >> 1));
}

static inline void put(uint8_t *dst, uint32_t px)
{
	dst[0] = (uint8_t) (px >> 24);
	dst[1] = (uint8_t) (px >> 16);
	dst[2] = (uint8_t) (px >> 8);
	dst[3] = (uint8_t) px;
}

bool texDecode(int fmt, int w, int h, const uint8_t *src, size_t srcBytes, uint8_t *dst)
{
	int bw, bh, bytes;
	if (!src || !dst || w <= 0 || h <= 0 || w > 4096 || h > 4096 || !texFormatInfo(fmt, &bw, &bh, &bytes))
		return false;
	if (srcBytes < texTiledBytes(fmt, w, h))
		return false;

	const int nx = (w + bw - 1) / bw;
	const int ny = (h + bh - 1) / bh;
	for (int by = 0; by < ny; by++) {
		for (int bx = 0; bx < nx; bx++) {
			const uint8_t *blk = src + ((size_t) by * nx + bx) * (size_t) bytes;
			for (int ty = 0; ty < bh; ty++) {
				int y = by * bh + ty;
				if (y >= h)
					break;
				for (int tx = 0; tx < bw; tx++) {
					int x = bx * bw + tx;
					if (x >= w)
						break;
					uint32_t px = 0;
					switch (fmt) {
					case GX_TF_I4:
					case GX_TF_CI4: {
						uint8_t b = blk[(ty * bw + tx) >> 1];
						uint32_t i = ((tx & 1) ? (b & 15) : (b >> 4)) * 17;
						px = rgba(i, i, i, i);
						break;
					}
					case GX_TF_I8:
					case GX_TF_CI8: {
						uint32_t i = blk[ty * bw + tx];
						px = rgba(i, i, i, i);
						break;
					}
					case GX_TF_IA4: {
						uint8_t b = blk[ty * bw + tx];
						uint32_t a = (b >> 4) * 17, i = (b & 15) * 17;
						px = rgba(i, i, i, a);
						break;
					}
					case GX_TF_IA8:
					case GX_TF_CI14: {
						const uint8_t *p = blk + (ty * bw + tx) * 2;
						uint32_t a = p[0], i = p[1];
						px = rgba(i, i, i, a);
						break;
					}
					case GX_TF_RGB565: {
						const uint8_t *p = blk + (ty * bw + tx) * 2;
						px = expand565((uint16_t) ((p[0] << 8) | p[1]));
						break;
					}
					case GX_TF_RGB5A3: {
						const uint8_t *p = blk + (ty * bw + tx) * 2;
						px = expand5A3((uint16_t) ((p[0] << 8) | p[1]));
						break;
					}
					case GX_TF_RGBA8: {
						int idx = (ty * bw + tx) * 2;
						px = rgba(blk[idx + 1], blk[32 + idx], blk[32 + idx + 1], blk[idx]);
						break;
					}
					}
					put(dst + ((size_t) y * w + x) * 4, px);
				}
			}
		}
	}
	return true;
}

} /* namespace gx2gx */
