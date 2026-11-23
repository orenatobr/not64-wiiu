/*
 * Gx2Core.cpp: the GX API (libogc signatures) implemented as a state machine.
 * Draw packets are handed to a Backend (GX2 on Wii U, a mock in the unit tests).
 * This file has no GX2/wut dependency.
 *
 * Threading: like libogc's GX, the API must be called from one thread.
 */
#include "../core/prof.h"
#include "Gx2Backend.h"
#include "Gx2Tev.h"
#include "Gx2Tex.h"
#include "Gx2Gx.h"
#include "compat/ogc/gx.h"
#include "compat/ogc/cache.h"
#include "compat/ogc/lwp_heap.h"

#if !defined(__APPLE__)
#include <malloc.h>
#endif
#include <map>
#include <stdlib.h>
#include <string.h>
#include <unordered_map>
#include <vector>

using namespace gx2gx;

namespace {

const int kEfbWidth = 640;                  /* logical (GX) EFB size */
const int kEfbHeight = 480;

struct TexEntry {
	TexHandle handle;
	int w, h, fmt;
	bool render;        /* GPU-resident (EFB copy), never re-decoded */
	size_t bytes;       /* extent in guest memory for invalidation */
};

struct Maps {
	GXTexObj obj;
	bool valid;
};

struct State {
	Backend *be;
	int scale;

	/* vertex description */
	uint8_t vcd[24];
	bool inPrim;
	uint8_t prim;
	uint32_t expect;
	std::vector<Vertex> verts;
	bool overflow;
	/* per-vertex assembly */
	uint32_t want, have;                    /* bit0 pos, bit1 colour, bit2 tex */
	int wantTex, haveTex, haveCol;
	float cur[3];
	uint8_t curCol[4];
	float curTex[8][2];
	int slotSrc[2];                         /* texgen source attribute index per shader texture slot */
	const float *slotMtx[2];
	float slotMtxStore[2][2][4];
	bool slotHasMtx[2];
	uint8_t colorDefault[4];

	/* matrices */
	Mtx posMtx[10];
	Mtx texMtx[10];
	Mtx44 proj;
	uint8_t projType;
	uint32_t curMtx;
	struct { uint8_t type, src, mtx; } texgen[8];

	/* pixel state */
	PipelineState pipe;
	uint8_t clipMode;
	uint8_t zTexOp;
	uint32_t zTexBias;
	uint8_t alphaComp0, alphaRef0, alphaOp, alphaComp1, alphaRef1;
	uint8_t fogType;
	float fogStart, fogEnd, fogNear, fogFar;
	GXColor fogColor;
	uint8_t dstAlphaEnable, dstAlpha;

	TevState tev;

	/* textures */
	Maps maps[8];
	std::map<uintptr_t, TexEntry> cache;
	TexHandle dummyTex;
	std::vector<uint8_t> decodeBuf;

	/* copies */
	int copyL, copyT, copyW, copyH;         /* tex copy source */
	int copyDstW, copyDstH, copyFmt;
	int dispL, dispT, dispW, dispH;
	GXColor clearColor;
	uint32_t clearZ;
	float gamma;
	GXDrawSyncCallback syncCb;
	uint32_t frame;

	std::unordered_map<void *, uint32_t> heapBlocks;
};

State S;
bool gInit = false;
float gNearZ = 0.0f, gFarZ = 1.0f;

inline void *alignedAlloc(size_t align, size_t n)
{
#if defined(__APPLE__)
	void *p = 0;
	return posix_memalign(&p, align, n) == 0 ? p : 0;
#else
	return memalign(align, n);
#endif
}

inline float f255(uint8_t v) { return (float) v / 255.0f; }

void invalidateRange(uintptr_t addr, size_t len, bool includeRender)
{
	if (!gInit || S.cache.empty() || len == 0)
		return;
	uintptr_t end = addr + len;
	std::map<uintptr_t, TexEntry>::iterator it = S.cache.lower_bound(addr);
	if (it != S.cache.begin()) {
		std::map<uintptr_t, TexEntry>::iterator prev = it;
		--prev;
		if (prev->first + prev->second.bytes > addr)
			it = prev;
	}
	while (it != S.cache.end() && it->first < end) {
		if (it->first + it->second.bytes > addr && (includeRender || !it->second.render)) {
			if (it->second.handle)
				S.be->destroyTexture(it->second.handle);
			it = S.cache.erase(it);
		} else {
			++it;
		}
	}
}

void setDefaults()
{
	memset(&S.pipe, 0, sizeof(S.pipe));
	S.pipe.cull = GX_CULL_NONE;
	S.pipe.zEnable = 1;
	S.pipe.zFunc = GX_LEQUAL;
	S.pipe.zUpdate = 1;
	S.pipe.colorUpdate = S.pipe.alphaUpdate = 1;
	S.pipe.zClip = 1;
	S.pipe.blend.type = GX_BM_NONE;
	S.pipe.blend.src = GX_BL_SRCALPHA;
	S.pipe.blend.dst = GX_BL_INVSRCALPHA;
	S.pipe.blend.logicOp = GX_LO_COPY;
	S.pipe.lineWidth = 1.0f;
	S.pipe.viewport[2] = kEfbWidth; S.pipe.viewport[3] = kEfbHeight;
	S.pipe.scissor[2] = kEfbWidth; S.pipe.scissor[3] = kEfbHeight;

	memset(S.vcd, 0, sizeof(S.vcd));
	for (int i = 0; i < 10; i++) {
		guMtxIdentity(S.posMtx[i]);
		guMtxIdentity(S.texMtx[i]);
	}
	guMtx44Identity(S.proj);
	S.projType = GX_PERSPECTIVE;
	S.curMtx = GX_PNMTX0;
	for (int i = 0; i < 8; i++) {
		S.texgen[i].type = GX_TG_MTX2x4;
		S.texgen[i].src = (uint8_t) (GX_TG_TEX0 + i);
		S.texgen[i].mtx = GX_IDENTITY;
	}
	S.alphaComp0 = GX_ALWAYS; S.alphaComp1 = GX_ALWAYS; S.alphaOp = GX_AOP_AND;
	S.fogType = GX_FOG_NONE;
	S.fogNear = 0.0f; S.fogFar = 1.0f;
	S.fogColor = GXColor{0, 0, 0, 255};
	S.dstAlphaEnable = 0; S.dstAlpha = 255;
	S.zTexOp = GX_ZT_DISABLE; S.zTexBias = 0;
	S.copyL = S.copyT = 0; S.copyW = kEfbWidth; S.copyH = kEfbHeight;
	S.dispL = S.dispT = 0; S.dispW = kEfbWidth; S.dispH = kEfbHeight;
	S.clearColor = GXColor{0, 0, 0, 255};
	S.clearZ = GX_MAX_Z24;
	S.gamma = 1.0f;
	tevReset(S.tev);
}

/* ---- blend / alpha helpers ---- */

AlphaTest makeAlphaTest()
{
	AlphaTest a;
	a.enable = 0; a.func = GX_ALWAYS; a.ref = 0.0f;
	uint8_t comp = GX_ALWAYS, ref = 0;
	if (S.alphaOp == GX_AOP_AND) {
		if (S.alphaComp0 != GX_ALWAYS) { comp = S.alphaComp0; ref = S.alphaRef0; }
		else if (S.alphaComp1 != GX_ALWAYS) { comp = S.alphaComp1; ref = S.alphaRef1; }
	} else if (S.alphaComp0 != GX_ALWAYS && S.alphaComp1 == GX_NEVER && S.alphaOp == GX_AOP_OR) {
		comp = S.alphaComp0; ref = S.alphaRef0;
	}
	/* other combinations of two real comparisons: only comp0 is honoured (see docs) */
	else if (S.alphaComp0 != GX_ALWAYS && S.alphaOp != GX_AOP_OR) {
		comp = S.alphaComp0; ref = S.alphaRef0;
	}
	if (comp == GX_ALWAYS)
		return a;
	a.enable = 1;
	a.func = comp;
	float r = (float) ref;
	switch (comp) {
	case GX_GREATER: r += 0.5f; break;
	case GX_LEQUAL: r += 0.5f; break;
	case GX_GEQUAL: r -= 0.5f; break;
	case GX_LESS: r -= 0.5f; break;
	default: break;
	}
	a.ref = r / 255.0f;
	return a;
}

/* ---- texture resolution ---- */

void texObjUnpack(const GXTexObj *o, void **ptr, int *w, int *h, int *fmt, SamplerDesc *s)
{
	uint64_t p = (uint64_t) o->val[0] | ((uint64_t) o->val[1] << 32);
	*ptr = (void *) (uintptr_t) p;
	*w = (int) (o->val[2] & 0xffff);
	*h = (int) (o->val[2] >> 16);
	*fmt = (int) (o->val[3] & 0xff);
	s->wrapS = (uint8_t) ((o->val[3] >> 8) & 0xff);
	s->wrapT = (uint8_t) ((o->val[3] >> 16) & 0xff);
	s->minLinear = (uint8_t) (o->val[4] & 0xff);
	s->magLinear = (uint8_t) ((o->val[4] >> 8) & 0xff);
	memcpy(&s->lodBias, &o->val[5], sizeof(float));
}

TexBind resolveTexture(int map)
{
	TexBind b;
	b.tex = S.dummyTex;
	b.sampler.wrapS = b.sampler.wrapT = GX_CLAMP;
	b.sampler.minLinear = b.sampler.magLinear = 0;
	b.sampler.lodBias = 0.0f;
	b.compMap = tevTexCompMap(S.tev, map);
	if (map < 0 || map > 7 || !S.maps[map].valid)
		return b;

	void *ptr; int w, h, fmt;
	texObjUnpack(&S.maps[map].obj, &ptr, &w, &h, &fmt, &b.sampler);
	if (!ptr || w <= 0 || h <= 0)
		return b;

	uintptr_t key = (uintptr_t) ptr;
	std::map<uintptr_t, TexEntry>::iterator it = S.cache.find(key);
	if (it != S.cache.end()) {
		TexEntry &e = it->second;
		if (e.render) {
			b.tex = e.handle;
			return b;
		}
		if (e.w == w && e.h == h && e.fmt == fmt && e.handle) {
			b.tex = e.handle;
			return b;
		}
		if (e.handle)
			S.be->destroyTexture(e.handle);
		S.cache.erase(it);
	}

	size_t tiled = texTiledBytes(fmt, w, h);
	if (tiled == 0)
		return b;
	S.decodeBuf.resize((size_t) w * h * 4);
	if (!texDecode(fmt, w, h, (const uint8_t *) ptr, tiled, &S.decodeBuf[0]))
		return b;
	TexHandle th = S.be->createTexture(w, h, &S.decodeBuf[0]);
	if (!th)
		return b;
	TexEntry e;
	e.handle = th; e.w = w; e.h = h; e.fmt = fmt; e.render = false; e.bytes = tiled;
	S.cache[key] = e;
	b.tex = th;
	return b;
}

/* ---- vertex assembly ---- */

int texSlotCoord(int slot)
{
	/* texcoord generator index feeding the stage that samples texmap `slot` */
	int n = S.tev.numStages > kMaxTevStages ? kMaxTevStages : S.tev.numStages;
	for (int i = 0; i < n; i++)
		if (S.tev.stage[i].texmap == slot)
			return S.tev.stage[i].texcoord == GX_TEXCOORD_NULL ? slot : S.tev.stage[i].texcoord;
	return slot;
}

void prepareSlots()
{
	for (int k = 0; k < 2; k++) {
		int c = texSlotCoord(k);
		S.slotSrc[k] = -1;
		S.slotHasMtx[k] = false;
		if (c < 0 || c > 7)
			continue;
		if (S.texgen[c].src >= GX_TG_TEX0 && S.texgen[c].src <= GX_TG_TEX7)
			S.slotSrc[k] = S.texgen[c].src - GX_TG_TEX0;
		uint8_t m = S.texgen[c].mtx;
		if (m != GX_IDENTITY && m >= GX_TEXMTX0 && ((m - GX_TEXMTX0) % 3) == 0 && (m - GX_TEXMTX0) / 3 < 10) {
			const Mtx &mt = S.texMtx[(m - GX_TEXMTX0) / 3];
			for (int r = 0; r < 2; r++)
				for (int q = 0; q < 4; q++)
					S.slotMtxStore[k][r][q] = mt[r][q];
			S.slotHasMtx[k] = true;
		}
	}
}

void emitVertex()
{
	if (S.verts.size() >= (size_t) S.expect) {
		S.overflow = true;
		return;
	}
	Vertex v;
	v.x = S.cur[0]; v.y = S.cur[1]; v.z = S.cur[2];
	if (S.want & 2) {
		v.r = S.curCol[0]; v.g = S.curCol[1]; v.b = S.curCol[2]; v.a = S.curCol[3];
	} else {
		v.r = v.g = v.b = v.a = 255;
	}
	float tc[2][2] = { { 0, 0 }, { 0, 0 } };
	for (int k = 0; k < 2; k++) {
		if (S.slotSrc[k] < 0)
			continue;
		float s = S.curTex[S.slotSrc[k]][0], t = S.curTex[S.slotSrc[k]][1];
		if (S.slotHasMtx[k]) {
			const float (*m)[4] = S.slotMtxStore[k];
			/* texture-coordinate input is (s, t, 1, 1) */
			float ns = m[0][0] * s + m[0][1] * t + m[0][2] + m[0][3];
			float nt = m[1][0] * s + m[1][1] * t + m[1][2] + m[1][3];
			s = ns; t = nt;
		}
		tc[k][0] = s; tc[k][1] = t;
	}
	v.s0 = tc[0][0]; v.t0 = tc[0][1]; v.s1 = tc[1][0]; v.t1 = tc[1][1];
	S.verts.push_back(v);
}

void attrDone(int kind)
{
	S.have |= (uint32_t) kind;
	/* a vertex is complete when every enabled attribute class has delivered all its values */
	bool colDone = !(S.want & 2) || S.haveCol >= 1;
	bool texDone = S.haveTex >= S.wantTex;
	if ((S.want & 1) && (S.have & 1) && colDone && texDone) {
		emitVertex();
		S.have = 0;
		S.haveCol = 0;
		S.haveTex = 0;
	}
}

Primitive mapPrim(uint8_t p, bool *ok)
{
	*ok = true;
	switch (p) {
	case GX_QUADS: return PRIM_QUADS;
	case GX_TRIANGLES: return PRIM_TRIANGLES;
	case GX_TRIANGLESTRIP: return PRIM_TRIANGLE_STRIP;
	case GX_TRIANGLEFAN: return PRIM_TRIANGLE_FAN;
	case GX_LINES: return PRIM_LINES;
	case GX_LINESTRIP: return PRIM_LINE_STRIP;
	case GX_POINTS: return PRIM_POINTS;
	}
	*ok = false;
	return PRIM_TRIANGLES;
}

uint32_t primMultiple(Primitive p)
{
	switch (p) {
	case PRIM_TRIANGLES: return 3;
	case PRIM_QUADS: return 4;
	case PRIM_LINES: return 2;
	default: return 1;
	}
}

void buildVs(VsParams *vs)
{
	/* GX transforms: p' = M * (x,y,z,1); the projection reads only the elements the XF unit uses. */
	const Mtx &M = S.posMtx[S.curMtx / 3 < 10 ? S.curMtx / 3 : 0];
	float Q[4][4];
	memset(Q, 0, sizeof(Q));
	const Mtx44 &P = S.proj;
	if (S.projType == GX_ORTHOGRAPHIC) {
		Q[0][0] = P[0][0]; Q[0][3] = P[0][3];
		Q[1][1] = P[1][1]; Q[1][3] = P[1][3];
		Q[2][2] = P[2][2]; Q[2][3] = P[2][3];
		Q[3][3] = 1.0f;
	} else {
		Q[0][0] = P[0][0]; Q[0][2] = P[0][2];
		Q[1][1] = P[1][1]; Q[1][2] = P[1][2];
		Q[2][2] = P[2][2]; Q[2][3] = P[2][3];
		Q[3][2] = -1.0f;
	}
	float T[4][4]; /* T = Q * [M; 0 0 0 1] */
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 4; c++) {
			float v = 0.0f;
			for (int k = 0; k < 3; k++)
				v += Q[r][k] * M[k][c];
			v += Q[r][3] * (c == 3 ? 1.0f : 0.0f);
			T[r][c] = v;
		}
	for (int c = 0; c < 4; c++)
		for (int r = 0; r < 4; r++)
			vs->col[c][r] = T[r][c];
	/* depth mapping (zScale/zOffset) is filled in by GX_End() from viewport and Z texture state */
	vs->zScale = 1.0f;
	vs->zOffset = 0.0f;
}

} /* namespace */

/* ------------------------------------------------------------------------ */
/* public control API                                                         */
/* ------------------------------------------------------------------------ */

namespace gx2gx {

void coreSetBackend(Backend *be, int efbScale)
{
	if (gInit) {
		for (std::map<uintptr_t, TexEntry>::iterator it = S.cache.begin(); it != S.cache.end(); ++it)
			if (it->second.handle && S.be)
				S.be->destroyTexture(it->second.handle);
		if (S.dummyTex && S.be)
			S.be->destroyTexture(S.dummyTex);
		S.cache.clear();
		S.dummyTex = 0;
		gInit = false;
	}
	if (!be)
		return;
	S.be = be;
	S.scale = efbScale < 1 ? 1 : (efbScale > 4 ? 4 : efbScale);
	S.verts.clear();
	S.inPrim = false;
	S.syncCb = 0;
	S.frame = 0;
	for (int i = 0; i < 8; i++)
		S.maps[i].valid = false;
	setDefaults();
	gNearZ = 0.0f; gFarZ = 1.0f;
	const uint8_t white[4] = { 255, 255, 255, 255 };
	S.dummyTex = be->createTexture(1, 1, white);
	gInit = true;
}

Backend *coreBackend() { return gInit ? S.be : 0; }
const PipelineState &coreDebugPipeline() { return S.pipe; }
float presentGamma() { return S.gamma; }

} /* namespace gx2gx */

/* ------------------------------------------------------------------------ */
/* libogc heap and cache shims                                                 */
/* ------------------------------------------------------------------------ */

extern "C" {

u32 __lwp_heap_init(heap_cntrl *h, void *start, u32 size, u32 pg)
{
	(void) start;
	h->budget = size;
	h->used = 0;
	h->pg_size = pg;
	return size;
}

void *__lwp_heap_allocate(heap_cntrl *h, u32 size)
{
	if (!h || size == 0)
		return 0;
	u32 rounded = (size + 31u) & ~31u;
	if (rounded < size || h->used + rounded > h->budget || h->used + rounded < h->used)
		return 0;
	void *p = alignedAlloc(32, rounded);
	if (!p)
		return 0;
	h->used += rounded;
	S.heapBlocks[p] = rounded;
	invalidateRange((uintptr_t) p, rounded, true);
	return p;
}

int __lwp_heap_free(heap_cntrl *h, void *ptr)
{
	if (!h || !ptr)
		return 0;
	std::unordered_map<void *, uint32_t>::iterator it = S.heapBlocks.find(ptr);
	if (it == S.heapBlocks.end())
		return 0;
	invalidateRange((uintptr_t) ptr, it->second, true);
	h->used -= it->second;
	S.heapBlocks.erase(it);
	free(ptr);
	return 1;
}

void gx2gx_DCFlushRange(void *p, u32 len) { invalidateRange((uintptr_t) p, len, false); }
void gx2gx_DCStoreRange(void *p, u32 len) { invalidateRange((uintptr_t) p, len, false); }
void gx2gx_DCInvalidateRange(void *p, u32 len) { (void) p; (void) len; }
void gx2gx_DCZeroRange(void *p, u32 len) { memset(p, 0, len); invalidateRange((uintptr_t) p, len, false); }

} /* extern "C" */

/* ------------------------------------------------------------------------ */
/* GX API                                                                      */
/* ------------------------------------------------------------------------ */

extern "C" {

void GX_ClearVtxDesc(void) { memset(S.vcd, 0, sizeof(S.vcd)); }

void GX_SetVtxDesc(u8 attr, u8 type)
{
	if (attr < sizeof(S.vcd))
		S.vcd[attr] = type;
}

void GX_SetVtxAttrFmt(u8 vtxfmt, u32 vtxattr, u32 comptype, u32 compsize, u32 frac)
{
	/* component types are implied by which GX_Position/Color/TexCoord call is used */
	(void) vtxfmt; (void) vtxattr; (void) comptype; (void) compsize; (void) frac;
}

void GX_Begin(u8 primitve, u8 vtxfmt, u16 vtxcnt)
{
	(void) vtxfmt;
	if (!gInit)
		return;
	S.prim = primitve;
	S.expect = vtxcnt;
	S.inPrim = true;
	S.overflow = false;
	S.verts.clear();
	S.verts.reserve(vtxcnt);
	S.want = 0;
	S.wantTex = 0;
	if (S.vcd[GX_VA_POS] == GX_DIRECT) S.want |= 1;
	if (S.vcd[GX_VA_CLR0] == GX_DIRECT) S.want |= 2;
	for (int i = 0; i < 8; i++)
		if (S.vcd[GX_VA_TEX0 + i] == GX_DIRECT)
			S.wantTex++;
	S.have = 0; S.haveCol = 0; S.haveTex = 0;
	S.cur[0] = S.cur[1] = S.cur[2] = 0.0f;
	memset(S.curTex, 0, sizeof(S.curTex));
	prepareSlots();
}

void GX_Position3f32(f32 x, f32 y, f32 z)
{
	if (!gInit || !S.inPrim) return;
	S.cur[0] = x; S.cur[1] = y; S.cur[2] = z;
	attrDone(1);
}

void GX_Position2f32(f32 x, f32 y) { GX_Position3f32(x, y, 0.0f); }

void GX_Color4u8(u8 r, u8 g, u8 b, u8 a)
{
	if (!gInit || !S.inPrim) return;
	S.curCol[0] = r; S.curCol[1] = g; S.curCol[2] = b; S.curCol[3] = a;
	S.haveCol++;
	attrDone(2);
}

void GX_TexCoord2f32(f32 s, f32 t)
{
	if (!gInit || !S.inPrim) return;
	/* n-th texcoord call feeds the n-th enabled TEX attribute */
	int n = S.haveTex, idx = -1;
	for (int i = 0; i < 8; i++) {
		if (S.vcd[GX_VA_TEX0 + i] == GX_DIRECT) {
			if (n == 0) { idx = i; break; }
			n--;
		}
	}
	if (idx >= 0) {
		S.curTex[idx][0] = s;
		S.curTex[idx][1] = t;
	}
	S.haveTex++;
	attrDone(4);
}

void GX_End(void)
{
	if (!gInit || !S.inPrim) return;
	S.inPrim = false;
	bool ok;
	Primitive p = mapPrim(S.prim, &ok);
	uint32_t n = (uint32_t) S.verts.size();
	if (!ok || n == 0)
		return;
	n -= n % primMultiple(p);
	if (p == PRIM_LINE_STRIP && n < 2) return;
	if ((p == PRIM_TRIANGLE_STRIP || p == PRIM_TRIANGLE_FAN) && n < 3) return;
	if (n == 0) return;

	static float ps[kPsUniformVec4 * 4];
	PsFixedParams fp;
	memset(&fp, 0, sizeof(fp));
	if (S.fogType != GX_FOG_NONE) {
		float range = S.fogEnd - S.fogStart;
		float inv = (range != 0.0f) ? 1.0f / range : 0.0f;
		float dz = S.fogFar - S.fogNear;
		fp.fogEnable = 1.0f;
		fp.fogA = dz * inv;
		fp.fogB = (S.fogNear - S.fogStart) * inv;
		fp.fogColor[0] = f255(S.fogColor.r); fp.fogColor[1] = f255(S.fogColor.g); fp.fogColor[2] = f255(S.fogColor.b);
	}
	fp.dstAlphaEnable = S.dstAlphaEnable ? 1.0f : 0.0f;
	fp.dstAlpha = f255(S.dstAlpha);

	DrawCall dc;
	memset(&dc, 0, sizeof(dc));
	dc.prim = p;
	dc.verts = &S.verts[0];
	dc.count = n;
	dc.pipe = S.pipe;
	dc.pipe.alphaTest = makeAlphaTest();
	buildVs(&dc.vs);
	{
		/* depth mapping: z_out = zc*(far-near) + wc*(far + ztex bias) */
		float nz = gNearZ, fz = gFarZ;
		float bias = (S.zTexOp != GX_ZT_DISABLE) ? (float) S.zTexBias / (float) GX_MAX_Z24 : 0.0f;
		if (S.zTexOp == GX_ZT_REPLACE) {
			dc.vs.zScale = 0.0f;
			dc.vs.zOffset = bias;
		} else {
			dc.vs.zScale = fz - nz;
			dc.vs.zOffset = fz + bias;
		}
	}
	dc.psStages = tevPack(S.tev, fp, ps);
	dc.ps = ps;
	dc.tex[0] = resolveTexture(0);
	dc.tex[1] = resolveTexture(1);
	PROF_BEGIN(PROF_DRAWS);
	S.be->draw(dc);
	PROF_END(PROF_DRAWS);
	S.verts.clear();
}

/* ---- matrices ---- */

void GX_LoadProjectionMtx(const Mtx44 mt, u8 type)
{
	memcpy(S.proj, mt, sizeof(Mtx44));
	S.projType = type;
}

void GX_LoadPosMtxImm(const Mtx mt, u32 pnidx)
{
	if (pnidx / 3 < 10 && pnidx % 3 == 0)
		memcpy(S.posMtx[pnidx / 3], mt, sizeof(Mtx));
}

void GX_LoadTexMtxImm(const Mtx mt, u32 texidx, u8 type)
{
	(void) type;
	if (texidx == GX_IDENTITY)
		return;
	if (texidx >= GX_TEXMTX0 && (texidx - GX_TEXMTX0) % 3 == 0 && (texidx - GX_TEXMTX0) / 3 < 10)
		memcpy(S.texMtx[(texidx - GX_TEXMTX0) / 3], mt, sizeof(Mtx));
}

void GX_SetCurrentMtx(u32 mtx) { S.curMtx = mtx; }

/* ---- raster / pixel state ---- */

void GX_SetViewport(f32 x, f32 y, f32 w, f32 h, f32 nearZ, f32 farZ)
{
	S.pipe.viewport[0] = x; S.pipe.viewport[1] = y; S.pipe.viewport[2] = w; S.pipe.viewport[3] = h;
	gNearZ = nearZ; gFarZ = farZ;
}

void GX_SetScissor(u32 x, u32 y, u32 w, u32 h)
{
	S.pipe.scissor[0] = (int) x; S.pipe.scissor[1] = (int) y; S.pipe.scissor[2] = (int) w; S.pipe.scissor[3] = (int) h;
}

void GX_SetCullMode(u8 mode) { S.pipe.cull = mode; }
void GX_SetClipMode(u8 mode) { S.clipMode = mode; S.pipe.zClip = (mode == GX_CLIP_ENABLE); }
void GX_SetCoPlanar(u8 enable) { (void) enable; }
void GX_SetLineWidth(u8 width, u8 fmt) { (void) fmt; S.pipe.lineWidth = (float) width / 6.0f; }
void GX_SetZMode(u8 enable, u8 func, u8 update) { S.pipe.zEnable = enable; S.pipe.zFunc = func; S.pipe.zUpdate = update; }
void GX_SetZCompLoc(u8 before_tex) { (void) before_tex; }
void GX_SetZTexture(u8 op, u8 fmt, u32 bias) { (void) fmt; S.zTexOp = op; S.zTexBias = bias; }

void GX_SetBlendMode(u8 type, u8 src, u8 dst, u8 op)
{
	S.pipe.blend.type = type; S.pipe.blend.src = src; S.pipe.blend.dst = dst; S.pipe.blend.logicOp = op;
}

void GX_SetColorUpdate(u8 enable) { S.pipe.colorUpdate = enable; }
void GX_SetAlphaUpdate(u8 enable) { S.pipe.alphaUpdate = enable; }
void GX_SetDstAlpha(u8 enable, u8 a) { S.dstAlphaEnable = enable; S.dstAlpha = a; }

void GX_SetAlphaCompare(u8 comp0, u8 ref0, u8 aop, u8 comp1, u8 ref1)
{
	S.alphaComp0 = comp0; S.alphaRef0 = ref0; S.alphaOp = aop; S.alphaComp1 = comp1; S.alphaRef1 = ref1;
}

void GX_SetFog(u8 type, f32 startz, f32 endz, f32 nearz, f32 farz, GXColor col)
{
	S.fogType = type; S.fogStart = startz; S.fogEnd = endz; S.fogNear = nearz; S.fogFar = farz; S.fogColor = col;
}

/* ---- TEV ---- */

void GX_SetNumChans(u8 num) { S.tev.numChans = num; }
void GX_SetNumTexGens(u32 nr) { S.tev.numTexGens = (uint8_t) (nr > 8 ? 8 : nr); }
void GX_SetNumTevStages(u8 num) { S.tev.numStages = num > kMaxTevStages ? kMaxTevStages : num; }

void GX_SetTexCoordGen(u16 texcoord, u32 type, u32 src, u32 mtx)
{
	if (texcoord < 8) {
		S.texgen[texcoord].type = (uint8_t) type;
		S.texgen[texcoord].src = (uint8_t) src;
		S.texgen[texcoord].mtx = (uint8_t) mtx;
	}
}

void GX_SetTevOrder(u8 stage, u8 texcoord, u32 texmap, u8 color)
{
	if (stage < kMaxTevStages) {
		TevStage &s = S.tev.stage[stage];
		s.texcoord = texcoord;
		s.texmap = (texmap & GX_TEXMAP_DISABLE) ? (uint8_t) GX_TEXMAP_NULL : (uint8_t) (texmap & 0xff);
		s.color = color;
	}
}

void GX_SetTevOp(u8 stage, u8 mode) { tevSetOp(S.tev, stage, mode); }

void GX_SetTevColorIn(u8 stage, u8 a, u8 b, u8 c, u8 d)
{
	if (stage < kMaxTevStages) {
		TevStage &s = S.tev.stage[stage];
		s.cIn[0] = a; s.cIn[1] = b; s.cIn[2] = c; s.cIn[3] = d;
	}
}

void GX_SetTevAlphaIn(u8 stage, u8 a, u8 b, u8 c, u8 d)
{
	if (stage < kMaxTevStages) {
		TevStage &s = S.tev.stage[stage];
		s.aIn[0] = a; s.aIn[1] = b; s.aIn[2] = c; s.aIn[3] = d;
	}
}

void GX_SetTevColorOp(u8 stage, u8 op, u8 bias, u8 scale, u8 clamp, u8 reg)
{
	if (stage < kMaxTevStages) {
		TevStage &s = S.tev.stage[stage];
		s.cOp = op; s.cBias = bias; s.cScale = scale; s.cClamp = clamp; s.cOut = reg & 3;
	}
}

void GX_SetTevAlphaOp(u8 stage, u8 op, u8 bias, u8 scale, u8 clamp, u8 reg)
{
	if (stage < kMaxTevStages) {
		TevStage &s = S.tev.stage[stage];
		s.aOp = op; s.aBias = bias; s.aScale = scale; s.aClamp = clamp; s.aOut = reg & 3;
	}
}

void GX_SetTevColor(u8 reg, GXColor c)
{
	if (reg >= 1 && reg <= 3) {
		S.tev.reg[reg][0] = f255(c.r); S.tev.reg[reg][1] = f255(c.g);
		S.tev.reg[reg][2] = f255(c.b); S.tev.reg[reg][3] = f255(c.a);
	}
}

void GX_SetTevKColor(u8 sel, GXColor c)
{
	if (sel < 4) {
		S.tev.kcolor[sel][0] = f255(c.r); S.tev.kcolor[sel][1] = f255(c.g);
		S.tev.kcolor[sel][2] = f255(c.b); S.tev.kcolor[sel][3] = f255(c.a);
	}
}

void GX_SetTevKColorSel(u8 stage, u8 sel) { if (stage < kMaxTevStages) S.tev.stage[stage].kcSel = sel; }
void GX_SetTevKAlphaSel(u8 stage, u8 sel) { if (stage < kMaxTevStages) S.tev.stage[stage].kaSel = sel; }

void GX_SetTevSwapMode(u8 stage, u8 ras, u8 tex)
{
	if (stage < kMaxTevStages) {
		S.tev.stage[stage].rasSwap = ras & 3;
		S.tev.stage[stage].texSwap = tex & 3;
	}
}

void GX_SetTevSwapModeTable(u8 id, u8 r, u8 g, u8 b, u8 a)
{
	if (id < 4) {
		S.tev.swap[id][0] = r & 3; S.tev.swap[id][1] = g & 3; S.tev.swap[id][2] = b & 3; S.tev.swap[id][3] = a & 3;
	}
}

/* ---- textures ---- */

void GX_InitTexObj(GXTexObj *obj, void *img, u16 w, u16 h, u8 fmt, u8 ws, u8 wt, u8 mip)
{
	uint64_t p = (uint64_t) (uintptr_t) img;
	obj->val[0] = (u32) (p & 0xffffffffu);
	obj->val[1] = (u32) (p >> 32);
	obj->val[2] = (u32) w | ((u32) h << 16);
	obj->val[3] = (u32) fmt | ((u32) ws << 8) | ((u32) wt << 16) | ((u32) mip << 24);
	obj->val[4] = (u32) 1 | ((u32) 1 << 8);   /* libogc default: linear/linear */
	float zero = 0.0f;
	memcpy(&obj->val[5], &zero, sizeof(float));
	obj->val[6] = obj->val[7] = 0;
}

void GX_InitTexObjLOD(GXTexObj *obj, u8 minf, u8 magf, f32 minlod, f32 maxlod, f32 bias, u8 biasclamp, u8 edgelod, u8 aniso)
{
	(void) minlod; (void) maxlod; (void) biasclamp; (void) edgelod; (void) aniso;
	obj->val[4] = (u32) (minf != GX_NEAR) | ((u32) (magf != GX_NEAR) << 8);
	memcpy(&obj->val[5], &bias, sizeof(float));
}

void GX_LoadTexObj(const GXTexObj *obj, u8 mapid)
{
	if (mapid < 8 && obj) {
		S.maps[mapid].obj = *obj;
		S.maps[mapid].valid = true;
	}
}

void GX_InvalidateTexAll(void)
{
	if (!gInit) return;
	invalidateRange(0, (size_t) -1, false);
}

/* ---- EFB copies ---- */

void GX_SetTexCopySrc(u16 l, u16 t, u16 w, u16 h) { S.copyL = l; S.copyT = t; S.copyW = w; S.copyH = h; }
void GX_SetTexCopyDst(u16 w, u16 h, u32 fmt, u8 mode) { (void) mode; S.copyDstW = w; S.copyDstH = h; S.copyFmt = (int) fmt; }
void GX_SetDispCopySrc(u16 l, u16 t, u16 w, u16 h) { S.dispL = l; S.dispT = t; S.dispW = w; S.dispH = h; }
void GX_SetDispCopyDst(u16 w, u16 h) { (void) w; (void) h; }
void GX_SetDispCopyFrame2Field(u8 mode) { (void) mode; }
void GX_SetDispCopyGamma(u8 gamma) { S.gamma = (gamma == GX_GM_2_2) ? 2.2f : (gamma == GX_GM_1_7 ? 1.7f : 1.0f); }
void GX_SetCopyFilter(u8 aa, const u8 sp[12][2], u8 vf, const u8 vfilter[7]) { (void) aa; (void) sp; (void) vf; (void) vfilter; }
void GX_SetCopyClear(GXColor c, u32 z) { S.clearColor = c; S.clearZ = z; }

static void doClear()
{
	unsigned mask = (S.pipe.colorUpdate ? 7u : 0u) | (S.pipe.alphaUpdate ? 8u : 0u);
	bool clearDepth = S.pipe.zUpdate != 0;
	if (!mask && !clearDepth)
		return;
	float c[4] = { f255(S.clearColor.r), f255(S.clearColor.g), f255(S.clearColor.b), f255(S.clearColor.a) };
	/* GX clear depth is a 24-bit value where GX_MAX_Z24 is the far plane */
	S.be->clearEfb(c, mask, clearDepth, (float) S.clearZ / (float) GX_MAX_Z24);
}

void GX_CopyTex(void *dest, u8 clear)
{
	if (!gInit) return;
	if (dest && S.copyDstW > 0 && S.copyDstH > 0) {
		uintptr_t key = (uintptr_t) dest;
		std::map<uintptr_t, TexEntry>::iterator it = S.cache.find(key);
		if (it != S.cache.end() && (!it->second.render || it->second.w != S.copyDstW || it->second.h != S.copyDstH)) {
			if (it->second.handle)
				S.be->destroyTexture(it->second.handle);
			S.cache.erase(it);
			it = S.cache.end();
		}
		if (it == S.cache.end()) {
			TexEntry e;
			e.handle = S.be->createRenderTexture(S.copyDstW, S.copyDstH);
			e.w = S.copyDstW; e.h = S.copyDstH; e.fmt = S.copyFmt; e.render = true;
			e.bytes = texTiledBytes(S.copyFmt, S.copyDstW, S.copyDstH);
			if (!e.handle)
				e.bytes = 0;
			if (e.bytes == 0)
				e.bytes = 32; /* still keyed by address so heap_free invalidates it */
			it = S.cache.insert(std::make_pair(key, e)).first;
		}
		if (it->second.handle)
			S.be->copyEfbToTexture(it->second.handle, S.copyDstW, S.copyDstH, S.copyL, S.copyT, S.copyW, S.copyH);
	}
	if (clear)
		doClear();
}

void GX_CopyDisp(void *dest, u8 clear)
{
	if (!gInit) return;
	if (dest) {
		S.be->present(S.dispW * S.scale, S.dispH * S.scale);
		S.frame++;
	}
	if (clear)
		doClear();
}

/* ---- synchronisation ---- */

void GX_PixModeSync(void) {}
void GX_DrawDone(void) { if (gInit) S.be->drawDone(); }
void GX_Flush(void) { if (gInit) S.be->flush(); }

void GX_SetDrawSync(u16 token)
{
	/* GX2 submission is ordered, so the token is reached as soon as everything before it is queued. */
	if (gInit && S.syncCb)
		S.syncCb(token);
}

GXDrawSyncCallback GX_SetDrawSyncCallback(GXDrawSyncCallback cb)
{
	GXDrawSyncCallback old = S.syncCb;
	S.syncCb = cb;
	return old;
}

void GX_RestoreWriteGatherPipe(void) {}
volatile void *GX_RedirectWriteGatherPipe(void *ptr) { return ptr; }

} /* extern "C" */
