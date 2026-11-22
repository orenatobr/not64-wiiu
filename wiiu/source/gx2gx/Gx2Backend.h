/*
 * Gx2Backend.h: the boundary between the GX API layer (Gx2Core.cpp, pure C++,
 * unit tested on the host) and the GX2 implementation (Gx2Wiiu.cpp, wut only).
 * The core records GX state and builds fully resolved draw packets; the backend
 * only turns packets into GX2 calls.
 */
#ifndef GX2GX_BACKEND_H
#define GX2GX_BACKEND_H

#include <stddef.h>
#include <stdint.h>
#include "Gx2Tev.h"

namespace gx2gx {

/* CPU-side vertex, uploaded as-is (32 bytes, attribute layout in Gx2Wiiu.cpp) */
struct Vertex {
	float x, y, z;
	uint8_t r, g, b, a;
	float s0, t0, s1, t1;
};

/* Opaque GPU texture handle owned by the backend (a GX2Texture on Wii U). */
typedef void *TexHandle;

struct BlendDesc {
	uint8_t type;      /* GX_BM_* */
	uint8_t src, dst;  /* GX_BL_* (already in GX semantics: see mapBlend*) */
	uint8_t logicOp;   /* GX_LO_* */
};

struct AlphaTest {
	uint8_t enable;    /* 0: always pass */
	uint8_t func;      /* GX compare (== GX2 compare numbering) */
	float ref;         /* 0..1, already adjusted for the 8-bit compare */
};

struct SamplerDesc {
	uint8_t wrapS, wrapT;   /* GX_CLAMP / GX_REPEAT / GX_MIRROR */
	uint8_t minLinear, magLinear;
	float lodBias;
};

struct TexBind {
	TexHandle tex;
	SamplerDesc sampler;
	uint32_t compMap;       /* GX2 component selectors, 0x00010203 = identity */
};

struct PipelineState {
	uint8_t cull;                      /* GX_CULL_* */
	uint8_t zEnable, zFunc, zUpdate;   /* GX_SetZMode */
	uint8_t colorUpdate, alphaUpdate;
	uint8_t zClip;                     /* clipping enabled */
	BlendDesc blend;
	AlphaTest alphaTest;
	float lineWidth;                   /* in EFB pixels */
	float viewport[4];                 /* x, y, w, h in logical EFB pixels */
	int scissor[4];                    /* x, y, w, h in logical EFB pixels */
};

/* Vertex shader constants: columns of (projection * model), then z mapping. */
struct VsParams {
	float col[4][4];                   /* clip = x*col0 + y*col1 + z*col2 + col3 */
	float zScale, zOffset;             /* z_out = zc*zScale + wc*zOffset */
};

enum Primitive { PRIM_POINTS, PRIM_LINES, PRIM_LINE_STRIP, PRIM_TRIANGLES, PRIM_TRIANGLE_STRIP, PRIM_TRIANGLE_FAN, PRIM_QUADS };

struct DrawCall {
	Primitive prim;
	const Vertex *verts;
	uint32_t count;
	PipelineState pipe;
	VsParams vs;
	const float *ps;                   /* kPsUniformVec4 * 4 floats */
	int psStages;                      /* 1..kShaderTevStages */
	TexBind tex[2];
};

class Backend {
public:
	virtual ~Backend() {}
	/* RGBA8 linear texture of w*h texels (rows w*4 bytes); returns 0 on failure. */
	virtual TexHandle createTexture(int w, int h, const uint8_t *rgba) = 0;
	/* RGBA8 texture usable as render target (EFB copy destination). */
	virtual TexHandle createRenderTexture(int w, int h) = 0;
	/* Deferred: the GPU may still read it for a few frames. */
	virtual void destroyTexture(TexHandle t) = 0;

	virtual void draw(const DrawCall &dc) = 0;

	/* Clear the EFB; mask bit 0..3 = r,g,b,a written, depth cleared when clearDepth. */
	virtual void clearEfb(const float rgba[4], unsigned colorMask, bool clearDepth, float depth) = 0;

	/* Copy a logical-pixel rectangle of the EFB colour into dst (scaling to dstW x dstH). */
	virtual void copyEfbToTexture(TexHandle dst, int dstW, int dstH, int x, int y, int w, int h) = 0;

	/* Frame finished: hand the EFB colour texture to the application. */
	virtual void present(int widthPx, int heightPx) = 0;

	virtual void drawDone() = 0;
	virtual void flush() = 0;
};

/* Installed by gx2gx::init() (Wii U) or by the unit tests (mock). */
void coreSetBackend(Backend *be, int efbScale);
Backend *coreBackend();

/* Test hooks */
const PipelineState &coreDebugPipeline();

} /* namespace gx2gx */

#endif
