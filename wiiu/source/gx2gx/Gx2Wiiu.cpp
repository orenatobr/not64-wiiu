/*
 * Gx2Wiiu.cpp: the GX2 backend of the GX shim (see Gx2Backend.h) plus gx2gx::init()/shutdown().
 * Wii U only (wut). The GX API layer lives in Gx2Core.cpp and is unit tested on the host.
 *
 * GX2 context contract: everything here runs in gx2gx's own GX2ContextState. The first GX call
 * of a frame (ensureFrame) selects it; present() ends the frame and the application must select
 * its own state again (WHBGfxBeginRender* does) before drawing, see Gx2Gx.h.
 */
#include <malloc.h>
#include <string.h>
#include <vector>

#include <coreinit/memdefaultheap.h>
#include <gx2/clear.h>
#include <gx2/context.h>
#include <gx2/draw.h>
#include <gx2/enum.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/sampler.h>
#include <gx2/shaders.h>
#include <gx2/state.h>
#include <gx2/surface.h>
#include <gx2/texture.h>

#include "Gx2Backend.h"
#include "Gx2Gx.h"
#include "Gx2Map.h"
#include "Gx2ShaderData.h"
#include "compat/ogc/gx.h"

#include "../drivers/wut/shaders/FetchShader.h"
#include "../drivers/wut/shaders/PixelShader.h"
#include "../drivers/wut/shaders/VertexShader.h"

using namespace gx2gx;

/* The numeric tables in Gx2Map.h must agree with wut's enums. */
static_assert((int) GX2_BLEND_MODE_ZERO == G2_BLEND_ZERO && (int) GX2_BLEND_MODE_ONE == G2_BLEND_ONE, "blend enum");
static_assert((int) GX2_BLEND_MODE_SRC_COLOR == G2_BLEND_SRC_COLOR && (int) GX2_BLEND_MODE_INV_SRC_COLOR == G2_BLEND_INV_SRC_COLOR, "blend enum");
static_assert((int) GX2_BLEND_MODE_SRC_ALPHA == G2_BLEND_SRC_ALPHA && (int) GX2_BLEND_MODE_INV_SRC_ALPHA == G2_BLEND_INV_SRC_ALPHA, "blend enum");
static_assert((int) GX2_BLEND_MODE_DST_ALPHA == G2_BLEND_DST_ALPHA && (int) GX2_BLEND_MODE_INV_DST_ALPHA == G2_BLEND_INV_DST_ALPHA, "blend enum");
static_assert((int) GX2_BLEND_MODE_DST_COLOR == G2_BLEND_DST_COLOR && (int) GX2_BLEND_MODE_INV_DST_COLOR == G2_BLEND_INV_DST_COLOR, "blend enum");
static_assert((int) GX2_LOGIC_OP_COPY == 0xCC && (int) GX2_LOGIC_OP_REV_AND == 0x44 && (int) GX2_LOGIC_OP_INV_AND == 0x22, "rop3 enum");
static_assert((int) GX2_TEX_CLAMP_MODE_WRAP == 0 && (int) GX2_TEX_CLAMP_MODE_MIRROR == 1 && (int) GX2_TEX_CLAMP_MODE_CLAMP == 2, "clamp enum");
static_assert((int) GX2_COMPARE_FUNC_NEVER == GX_NEVER && (int) GX2_COMPARE_FUNC_LESS == GX_LESS && (int) GX2_COMPARE_FUNC_EQUAL == GX_EQUAL &&
              (int) GX2_COMPARE_FUNC_LEQUAL == GX_LEQUAL && (int) GX2_COMPARE_FUNC_GREATER == GX_GREATER &&
              (int) GX2_COMPARE_FUNC_NOT_EQUAL == GX_NEQUAL && (int) GX2_COMPARE_FUNC_GEQUAL == GX_GEQUAL &&
              (int) GX2_COMPARE_FUNC_ALWAYS == GX_ALWAYS, "compare enum");
static_assert(sizeof(Vertex) == 32, "vertex layout is part of the shader contract");

/*
 * Open question for hardware: which winding GX calls "front". glN64 flips N64's front face
 * for GX ("GC vertex winding is backwards"), i.e. GX front = clockwise as seen on screen.
 * Flip this constant if triangles are culled the wrong way round.
 */
#ifndef GX2GX_FRONT_FACE
#define GX2GX_FRONT_FACE GX2_FRONT_FACE_CW
#endif

namespace {

const int kBaseW = 640;
const int kBaseH = 480;
const uint32_t kRingBytes = 8u * 1024u * 1024u;
const int kFramesBeforeFree = 4;

struct GpuTex {
	GX2Texture tex;
	GX2ColorBuffer cb;      /* only valid when render != 0 */
	int render;
	int retireFrame;
};

struct Backend2 : Backend {
	GX2ContextState *ctx;
	bool inFrame;
	bool stateKnown;
	int scale;
	int efbW, efbH;         /* pixels */

	GX2ColorBuffer efbCb;
	GX2Texture efbTex;
	GpuTex *dummy;           /* 1x1 white, bound to texture units nothing else is bound to */
	GpuTex *presentTex[2];   /* frames handed to the app: valid until the second following present */
	int presentParity;
	GX2DepthBuffer depth;
	void *hiZ;

	VertexShader *vs;
	FetchShader *fetch;
	PixelShader *ps[kShaderTevStages];
	int curPs;

	uint8_t *ring;
	uint32_t ringPos;

	PipelineState last;
	bool lastValid;
	float lastVs[20];
	bool lastVsValid;
	float lastPs[kPsUniformVec4 * 4];
	int lastPsRows;

	GX2Sampler samplers[3][3][2][2];
	bool samplerInit[3][3][2][2];

	std::vector<GpuTex *> retired;
	int frame;
	uint32_t texCount;

	Backend2() : ctx(0), inFrame(false), stateKnown(false), scale(1), efbW(0), efbH(0), presentParity(0), hiZ(0), vs(0), fetch(0),
	             curPs(-1), ring(0), ringPos(0), lastValid(false), lastVsValid(false), lastPsRows(0), frame(0), texCount(0)
	{
		memset(&efbCb, 0, sizeof(efbCb));
		memset(&efbTex, 0, sizeof(efbTex));
		presentTex[0] = presentTex[1] = 0;
		dummy = 0;
		memset(&depth, 0, sizeof(depth));
		memset(&last, 0, sizeof(last));
		memset(ps, 0, sizeof(ps));
		memset(samplerInit, 0, sizeof(samplerInit));
	}

	bool setup(int s);
	void teardown();

	/* Backend interface */
	TexHandle createTexture(int w, int h, const uint8_t *rgba);
	TexHandle createRenderTexture(int w, int h);
	void destroyTexture(TexHandle t);
	void draw(const DrawCall &dc);
	void clearEfb(const float rgba[4], unsigned colorMask, bool clearDepth, float depthValue);
	void copyEfbToTexture(TexHandle dst, int dstW, int dstH, int x, int y, int w, int h);
	void present(int widthPx, int heightPx);
	void drawDone() { GX2DrawDone(); }
	void flush() { GX2Flush(); }

	/* helpers */
	void ensureFrame();
	void bindEfb();
	void forceStateReload();
	void applyPipeline(const PipelineState &p, int targetW, int targetH, bool toEfb);
	void issue(const DrawCall &dc, bool toEfb, int targetW, int targetH);
	const GX2Sampler *sampler(const SamplerDesc &d);
	uint8_t *ringAlloc(uint32_t bytes);
	void releaseRetired(bool all);
	static bool makeSurface(GX2Surface *s, int w, int h, GX2SurfaceUse use, GX2SurfaceFormat fmt);
};

Backend2 *gBackend = 0;

bool Backend2::makeSurface(GX2Surface *s, int w, int h, GX2SurfaceUse use, GX2SurfaceFormat fmt)
{
	s->use = use;
	s->dim = GX2_SURFACE_DIM_TEXTURE_2D;
	s->width = (uint32_t) w;
	s->height = (uint32_t) h;
	s->depth = 1;
	s->mipLevels = 1;
	s->format = fmt;
	s->aa = GX2_AA_MODE1X;
	s->tileMode = GX2_TILE_MODE_DEFAULT;
	GX2CalcSurfaceSizeAndAlignment(s);
	s->image = MEMAllocFromDefaultHeapEx(s->imageSize, s->alignment);
	if (!s->image)
		return false;
	memset(s->image, 0, s->imageSize);
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU, s->image, s->imageSize);
	return true;
}

void texViewOf(GX2Texture *t, const GX2Surface &s)
{
	memset(t, 0, sizeof(*t));
	t->surface = s;
	t->viewFirstMip = 0;
	t->viewNumMips = 1;
	t->viewFirstSlice = 0;
	t->viewNumSlices = 1;
	t->compMap = 0x00010203;
	GX2InitTextureRegs(t);
}

bool Backend2::setup(int s)
{
	scale = s;
	efbW = kBaseW * s;
	efbH = kBaseH * s;

	ctx = (GX2ContextState *) MEMAllocFromDefaultHeapEx(sizeof(GX2ContextState), GX2_CONTEXT_STATE_ALIGNMENT);
	ring = (uint8_t *) MEMAllocFromDefaultHeapEx(kRingBytes, GX2_VERTEX_BUFFER_ALIGNMENT);
	if (!ctx || !ring)
		return false;

	/* colour: RGBA8, usable as render target and as texture (handed to the app) */
	memset(&efbCb, 0, sizeof(efbCb));
	if (!makeSurface(&efbCb.surface, efbW, efbH, (GX2SurfaceUse) (GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER),
	                 GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8))
		return false;
	efbCb.viewNumSlices = 1;
	GX2InitColorBufferRegs(&efbCb);
	texViewOf(&efbTex, efbCb.surface);

	{
		const uint8_t white[4] = { 255, 255, 255, 255 };
		dummy = (GpuTex *) createTexture(1, 1, white);
		if (!dummy)
			return false;
	}
	for (int i = 0; i < 2; i++) {
		presentTex[i] = (GpuTex *) createRenderTexture(efbW, efbH);
		if (!presentTex[i])
			return false;
	}

	/* depth: D24S8 float, no HiZ */
	memset(&depth, 0, sizeof(depth));
	if (!makeSurface(&depth.surface, efbW, efbH, GX2_SURFACE_USE_DEPTH_BUFFER, GX2_SURFACE_FORMAT_FLOAT_D24_S8))
		return false;
	depth.viewNumSlices = 1;
	depth.depthClear = 1.0f;
	depth.stencilClear = 0;
	GX2InitDepthBufferRegs(&depth);

	/* shaders through dborth's helper classes */
	vs = new VertexShader(4);
	vs->setProgram(kTev1_VsProgram, sizeof(kTev1_VsProgram), kTev1_VsRegs, sizeof(kTev1_VsRegs));
	vs->addUniformVar((GX2UniformVar){ "vs_matrix", GX2_SHADER_VAR_TYPE_FLOAT4, 4, 0, -1 });
	vs->addUniformVar((GX2UniformVar){ "vs_depth", GX2_SHADER_VAR_TYPE_FLOAT4, 1, 16, -1 });
	vs->addAttribVar((GX2AttribVar){ "attr_position", GX2_SHADER_VAR_TYPE_FLOAT3, 0, 0 });
	vs->addAttribVar((GX2AttribVar){ "attr_color", GX2_SHADER_VAR_TYPE_FLOAT4, 0, 1 });
	vs->addAttribVar((GX2AttribVar){ "attr_tex0", GX2_SHADER_VAR_TYPE_FLOAT2, 0, 2 });
	vs->addAttribVar((GX2AttribVar){ "attr_tex1", GX2_SHADER_VAR_TYPE_FLOAT2, 0, 3 });
	GX2InitAttribStream(vs->getAttributeBuffer(0), 0, 0, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32_32);
	GX2InitAttribStream(vs->getAttributeBuffer(1), 1, 0, 12, GX2_ATTRIB_FORMAT_UNORM_8_8_8_8);
	GX2InitAttribStream(vs->getAttributeBuffer(2), 2, 0, 16, GX2_ATTRIB_FORMAT_FLOAT_32_32);
	GX2InitAttribStream(vs->getAttributeBuffer(3), 3, 0, 24, GX2_ATTRIB_FORMAT_FLOAT_32_32);
	fetch = new FetchShader(vs->getAttributeBuffer(), vs->getAttributesCount());

	struct PsData { const uint32_t *prog; uint32_t progSize; const uint32_t *regs; uint32_t regsSize; };
	const PsData data[kShaderTevStages] = {
		{ kTev1_PsProgram, sizeof(kTev1_PsProgram), kTev1_PsRegs, sizeof(kTev1_PsRegs) },
		{ kTev2_PsProgram, sizeof(kTev2_PsProgram), kTev2_PsRegs, sizeof(kTev2_PsRegs) },
		{ kTev3_PsProgram, sizeof(kTev3_PsProgram), kTev3_PsRegs, sizeof(kTev3_PsRegs) },
		{ kTev4_PsProgram, sizeof(kTev4_PsProgram), kTev4_PsRegs, sizeof(kTev4_PsRegs) },
		{ kTev5_PsProgram, sizeof(kTev5_PsProgram), kTev5_PsRegs, sizeof(kTev5_PsRegs) },
		{ kTev6_PsProgram, sizeof(kTev6_PsProgram), kTev6_PsRegs, sizeof(kTev6_PsRegs) },
		{ kTev7_PsProgram, sizeof(kTev7_PsProgram), kTev7_PsRegs, sizeof(kTev7_PsRegs) },
	};
	for (int i = 0; i < kShaderTevStages; i++) {
		ps[i] = new PixelShader();
		ps[i]->setProgram(data[i].prog, data[i].progSize, data[i].regs, data[i].regsSize);
		ps[i]->addUniformVar((GX2UniformVar){ "ps_regs", GX2_SHADER_VAR_TYPE_FLOAT4, kPsUniformVec4, 0, -1 });
		ps[i]->addSamplerVar((GX2SamplerVar){ "s_tex0", GX2_SAMPLER_VAR_TYPE_SAMPLER_2D, 0 });
		ps[i]->addSamplerVar((GX2SamplerVar){ "s_tex1", GX2_SAMPLER_VAR_TYPE_SAMPLER_2D, 1 });
	}

	GX2SetupContextStateEx(ctx, TRUE);
	GX2SetContextState(ctx);
	GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
	inFrame = true;       /* context is selected right now */
	bindEfb();
	stateKnown = false;
	return true;
}

void Backend2::teardown()
{
	GX2DrawDone();
	if (dummy) {
		destroyTexture(dummy);
		dummy = 0;
	}
	for (int i = 0; i < 2; i++) {
		if (presentTex[i]) {
			destroyTexture(presentTex[i]);
			presentTex[i] = 0;
		}
	}
	releaseRetired(true);
	for (int i = 0; i < kShaderTevStages; i++) {
		delete ps[i];
		ps[i] = 0;
	}
	delete fetch;
	delete vs;
	fetch = 0;
	vs = 0;
	if (efbCb.surface.image)
		MEMFreeToDefaultHeap(efbCb.surface.image);
	if (depth.surface.image)
		MEMFreeToDefaultHeap(depth.surface.image);
	if (ring)
		MEMFreeToDefaultHeap(ring);
	if (ctx)
		MEMFreeToDefaultHeap(ctx);
	memset(&efbCb, 0, sizeof(efbCb));
	memset(&depth, 0, sizeof(depth));
	ring = 0;
	ctx = 0;
}

void Backend2::bindEfb()
{
	GX2SetColorBuffer(&efbCb, GX2_RENDER_TARGET_0);
	GX2SetDepthBuffer(&depth);
}

void Backend2::forceStateReload()
{
	lastValid = false;
	lastVsValid = false;
	curPs = -1;
	lastPsRows = 0;
	stateKnown = false;
}

void Backend2::ensureFrame()
{
	if (inFrame)
		return;
	GX2SetContextState(ctx);
	GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
	bindEfb();
	forceStateReload();
	inFrame = true;
}

uint8_t *Backend2::ringAlloc(uint32_t bytes)
{
	uint32_t need = (bytes + (GX2_VERTEX_BUFFER_ALIGNMENT - 1)) & ~(uint32_t) (GX2_VERTEX_BUFFER_ALIGNMENT - 1);
	if (need > kRingBytes)
		return 0;
	if (ringPos + need > kRingBytes) {
		/* the GPU may still read the start of the ring: drain before reusing it */
		GX2Flush();
		GX2DrawDone();
		ringPos = 0;
	}
	uint8_t *p = ring + ringPos;
	ringPos += need;
	return p;
}

const GX2Sampler *Backend2::sampler(const SamplerDesc &d)
{
	int ws = d.wrapS == GX_REPEAT ? 0 : (d.wrapS == GX_MIRROR ? 1 : 2);
	int wt = d.wrapT == GX_REPEAT ? 0 : (d.wrapT == GX_MIRROR ? 1 : 2);
	int mn = d.minLinear ? 1 : 0, mg = d.magLinear ? 1 : 0;
	GX2Sampler &s = samplers[ws][wt][mn][mg];
	if (!samplerInit[ws][wt][mn][mg]) {
		GX2InitSampler(&s, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_POINT);
		GX2InitSamplerClamping(&s, (GX2TexClampMode) mapWrap(d.wrapS), (GX2TexClampMode) mapWrap(d.wrapT), GX2_TEX_CLAMP_MODE_WRAP);
		GX2InitSamplerXYFilter(&s, mg ? GX2_TEX_XY_FILTER_MODE_LINEAR : GX2_TEX_XY_FILTER_MODE_POINT,
		                       mn ? GX2_TEX_XY_FILTER_MODE_LINEAR : GX2_TEX_XY_FILTER_MODE_POINT, GX2_TEX_ANISO_RATIO_NONE);
		samplerInit[ws][wt][mn][mg] = true;
	}
	return &s;
}

void Backend2::applyPipeline(const PipelineState &p, int tw, int th, bool toEfb)
{
	const bool all = !lastValid;
	const PipelineState &o = last;

	if (all || p.zEnable != o.zEnable || p.zFunc != o.zFunc || p.zUpdate != o.zUpdate)
		GX2SetDepthOnlyControl(p.zEnable ? TRUE : FALSE, (p.zEnable && p.zUpdate) ? TRUE : FALSE, (GX2CompareFunction) p.zFunc);
	if (all || p.cull != o.cull)
		GX2SetCullOnlyControl(GX2GX_FRONT_FACE, (p.cull == GX_CULL_FRONT || p.cull == GX_CULL_ALL) ? TRUE : FALSE,
		                      (p.cull == GX_CULL_BACK || p.cull == GX_CULL_ALL) ? TRUE : FALSE);
	if (all || p.colorUpdate != o.colorUpdate || p.alphaUpdate != o.alphaUpdate) {
		GX2ChannelMask m = (GX2ChannelMask) mapChannelMask(p.colorUpdate != 0, p.alphaUpdate != 0);
		GX2SetTargetChannelMasks(m, (GX2ChannelMask) 0, (GX2ChannelMask) 0, (GX2ChannelMask) 0,
		                         (GX2ChannelMask) 0, (GX2ChannelMask) 0, (GX2ChannelMask) 0, (GX2ChannelMask) 0);
	}
	if (all || p.blend.type != o.blend.type || p.blend.src != o.blend.src || p.blend.dst != o.blend.dst || p.blend.logicOp != o.blend.logicOp) {
		switch (p.blend.type) {
		case GX_BM_BLEND: {
			GX2BlendMode s = (GX2BlendMode) mapBlendFactor(p.blend.src, false);
			GX2BlendMode d = (GX2BlendMode) mapBlendFactor(p.blend.dst, true);
			GX2SetBlendControl(GX2_RENDER_TARGET_0, s, d, GX2_BLEND_COMBINE_MODE_ADD, TRUE, s, d, GX2_BLEND_COMBINE_MODE_ADD);
			GX2SetColorControl(GX2_LOGIC_OP_COPY, 0x01, FALSE, TRUE);
			break;
		}
		case GX_BM_SUBTRACT:
			GX2SetBlendControl(GX2_RENDER_TARGET_0, GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_ONE, GX2_BLEND_COMBINE_MODE_REV_SUB, TRUE,
			                   GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_ONE, GX2_BLEND_COMBINE_MODE_REV_SUB);
			GX2SetColorControl(GX2_LOGIC_OP_COPY, 0x01, FALSE, TRUE);
			break;
		case GX_BM_LOGIC:
			GX2SetColorControl((GX2LogicOp) mapLogicOp(p.blend.logicOp), 0x00, FALSE, TRUE);
			break;
		default:
			GX2SetColorControl(GX2_LOGIC_OP_COPY, 0x00, FALSE, TRUE);
			break;
		}
	}
	if (all || p.alphaTest.enable != o.alphaTest.enable || p.alphaTest.func != o.alphaTest.func || p.alphaTest.ref != o.alphaTest.ref)
		GX2SetAlphaTest(p.alphaTest.enable ? TRUE : FALSE, (GX2CompareFunction) p.alphaTest.func, p.alphaTest.ref);
	if (all || p.zClip != o.zClip)
		GX2SetRasterizerClipControlHalfZ(TRUE, p.zClip ? TRUE : FALSE, TRUE);
	if (all || p.lineWidth != o.lineWidth)
		GX2SetLineWidth(p.lineWidth * (float) scale > 0.0f ? p.lineWidth * (float) scale : 1.0f);

	const float sc = toEfb ? (float) scale : 1.0f;
	if (all || memcmp(p.viewport, o.viewport, sizeof(p.viewport)) != 0 || !toEfb) {
		float vw = p.viewport[2] * sc, vh = p.viewport[3] * sc;
		GX2SetViewport(p.viewport[0] * sc, p.viewport[1] * sc, vw < 1.0f ? 1.0f : vw, vh < 1.0f ? 1.0f : vh, 0.0f, 1.0f);
	}
	if (all || memcmp(p.scissor, o.scissor, sizeof(p.scissor)) != 0 || !toEfb) {
		int x = (int) ((float) p.scissor[0] * sc), y = (int) ((float) p.scissor[1] * sc);
		int w = (int) ((float) p.scissor[2] * sc), h = (int) ((float) p.scissor[3] * sc);
		if (x < 0) { w += x; x = 0; }
		if (y < 0) { h += y; y = 0; }
		if (x > tw) x = tw;
		if (y > th) y = th;
		if (w < 0) w = 0;
		if (h < 0) h = 0;
		if (x + w > tw) w = tw - x;
		if (y + h > th) h = th - y;
		GX2SetScissor((uint32_t) x, (uint32_t) y, (uint32_t) w, (uint32_t) h);
	}
	if (toEfb) {
		last = p;
		lastValid = true;
	} else {
		lastValid = false;    /* a pass into another target clobbered the cached state */
	}
}

void Backend2::issue(const DrawCall &dc, bool toEfb, int tw, int th)
{
	if (!dc.count || dc.psStages < 1 || dc.psStages > kShaderTevStages)
		return;
	const uint32_t bytes = dc.count * (uint32_t) sizeof(Vertex);
	uint8_t *dst = ringAlloc(bytes);
	if (!dst)
		return;   /* more than 8 MiB in a single draw: cannot happen with u16 vertex counts */
	memcpy(dst, dc.verts, bytes);
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, dst, bytes);

	applyPipeline(dc.pipe, tw, th, toEfb);

	if (curPs < 0) {
		fetch->setShader();
		vs->setShader();
	}
	if (curPs != dc.psStages) {
		ps[dc.psStages - 1]->setShader();
		curPs = dc.psStages;
		lastPsRows = 0;   /* uniform registers are not guaranteed to survive a shader switch */
	}

	float vsu[20];
	for (int c = 0; c < 4; c++)
		for (int r = 0; r < 4; r++)
			vsu[c * 4 + r] = dc.vs.col[c][r];
	vsu[16] = dc.vs.zScale; vsu[17] = dc.vs.zOffset; vsu[18] = 0.0f; vsu[19] = 0.0f;
	if (!lastVsValid || memcmp(vsu, lastVs, sizeof(vsu)) != 0) {
		GX2SetVertexUniformReg(0, 20, vsu);
		memcpy(lastVs, vsu, sizeof(vsu));
		lastVsValid = true;
	}

	const int rows = kPsStageBase + dc.psStages * kPsStageSize;
	if (rows != lastPsRows || memcmp(dc.ps, lastPs, sizeof(float) * 4 * rows) != 0) {
		GX2SetPixelUniformReg(0, (uint32_t) (rows * 4), dc.ps);
		memcpy(lastPs, dc.ps, sizeof(float) * 4 * rows);
		lastPsRows = rows;
	}

	for (int u = 0; u < 2; u++) {
		const GpuTex *gt = (const GpuTex *) dc.tex[u].tex;
		if (!gt)
			gt = dummy;   /* the shader samples both units, so both must be bound */
		if (dc.tex[u].compMap != 0x00010203) {
			GX2Texture tmp = gt->tex;
			tmp.compMap = dc.tex[u].compMap;
			GX2InitTextureRegs(&tmp);
			GX2SetPixelTexture(&tmp, (uint32_t) u);
		} else {
			GX2SetPixelTexture(&gt->tex, (uint32_t) u);
		}
		GX2SetPixelSampler(sampler(dc.tex[u].sampler), (uint32_t) u);
	}

	GX2SetAttribBuffer(0, bytes, (uint32_t) sizeof(Vertex), dst);

	GX2PrimitiveMode mode = GX2_PRIMITIVE_MODE_TRIANGLES;
	switch (dc.prim) {
	case PRIM_POINTS: mode = GX2_PRIMITIVE_MODE_POINTS; break;
	case PRIM_LINES: mode = GX2_PRIMITIVE_MODE_LINES; break;
	case PRIM_LINE_STRIP: mode = GX2_PRIMITIVE_MODE_LINE_STRIP; break;
	case PRIM_TRIANGLES: mode = GX2_PRIMITIVE_MODE_TRIANGLES; break;
	case PRIM_TRIANGLE_STRIP: mode = GX2_PRIMITIVE_MODE_TRIANGLE_STRIP; break;
	case PRIM_TRIANGLE_FAN: mode = GX2_PRIMITIVE_MODE_TRIANGLE_FAN; break;
	case PRIM_QUADS: mode = GX2_PRIMITIVE_MODE_QUADS; break;
	}
	GX2DrawEx(mode, dc.count, 0, 1);
}

void Backend2::draw(const DrawCall &dc)
{
	ensureFrame();
	issue(dc, true, efbW, efbH);
}

/* a draw packet that renders a full-screen quad with a 1-stage TEV program */
static void makeQuadCall(DrawCall *dc, Vertex quad[4], const PipelineState &pipe, float *psBuf, const TevState &tev,
                         const PsFixedParams &fp)
{
	dc->prim = PRIM_QUADS;
	dc->verts = quad;
	dc->count = 4;
	dc->pipe = pipe;
	memset(&dc->vs, 0, sizeof(dc->vs));
	dc->vs.col[0][0] = 1.0f;     /* x */
	dc->vs.col[1][1] = 1.0f;     /* y */
	dc->vs.col[2][2] = 1.0f;     /* z */
	dc->vs.col[3][3] = 1.0f;     /* w = 1 */
	dc->vs.zScale = 1.0f;
	dc->vs.zOffset = 0.0f;
	dc->psStages = tevPack(tev, fp, psBuf);
	dc->ps = psBuf;
}

static PipelineState flatPipeline(int w, int h, unsigned mask)
{
	PipelineState p;
	memset(&p, 0, sizeof(p));
	p.cull = GX_CULL_NONE;
	p.zEnable = 0;
	p.zFunc = GX_ALWAYS;
	p.zUpdate = 0;
	p.colorUpdate = (mask & 7) ? 1 : 0;
	p.alphaUpdate = (mask & 8) ? 1 : 0;
	p.zClip = 1;
	p.blend.type = GX_BM_NONE;
	p.blend.src = GX_BL_ONE;
	p.blend.dst = GX_BL_ZERO;
	p.blend.logicOp = GX_LO_COPY;
	p.alphaTest.enable = 0;
	p.alphaTest.func = GX_ALWAYS;
	p.lineWidth = 1.0f;
	p.viewport[2] = (float) w; p.viewport[3] = (float) h;
	p.scissor[2] = w; p.scissor[3] = h;
	return p;
}

void Backend2::clearEfb(const float rgba[4], unsigned colorMask, bool clearDepth, float depthValue)
{
	ensureFrame();
	if (colorMask == 15) {
		GX2ClearColor(&efbCb, rgba[0], rgba[1], rgba[2], rgba[3]);
		GX2SetContextState(ctx);   /* GX2Clear* touch the context (same as WHBGfxClearColor) */
		GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
		bindEfb();
		forceStateReload();
	} else if (colorMask) {
		/* masked clear: full-screen flat quad through the TEV shader with the clear colour as vertex colour */
		TevState tev;
		tevReset(tev);
		tev.stage[0].texmap = GX_TEXMAP_NULL;
		tev.stage[0].color = GX_COLOR0A0;
		tevSetOp(tev, 0, GX_PASSCLR);
		PsFixedParams fp;
		memset(&fp, 0, sizeof(fp));
		static float psBuf[kPsUniformVec4 * 4];
		Vertex q[4];
		memset(q, 0, sizeof(q));
		const float xs[4] = { -1, 1, 1, -1 }, ys[4] = { 1, 1, -1, -1 };
		for (int i = 0; i < 4; i++) {
			q[i].x = xs[i]; q[i].y = ys[i];
			q[i].r = (uint8_t) (rgba[0] * 255.0f + 0.5f); q[i].g = (uint8_t) (rgba[1] * 255.0f + 0.5f);
			q[i].b = (uint8_t) (rgba[2] * 255.0f + 0.5f); q[i].a = (uint8_t) (rgba[3] * 255.0f + 0.5f);
		}
		DrawCall dc;
		memset(&dc, 0, sizeof(dc));
		makeQuadCall(&dc, q, flatPipeline(efbW, efbH, colorMask), psBuf, tev, fp);
		dc.tex[0].tex = dc.tex[1].tex = 0;
		/* the viewport/scissor of a flat pipeline are in "pixels", so issue with scale 1 semantics */
		const int s = scale;
		scale = 1;
		issue(dc, true, efbW, efbH);
		scale = s;
		lastValid = false;
	}
	if (clearDepth) {
		GX2ClearDepthStencilEx(&depth, depthValue, 0, GX2_CLEAR_FLAGS_DEPTH);
		GX2SetContextState(ctx);
		GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
		bindEfb();
		forceStateReload();
	}
}

void Backend2::copyEfbToTexture(TexHandle dstH, int dstW, int dstH_, int x, int y, int w, int h)
{
	GpuTex *dst = (GpuTex *) dstH;
	if (!dst || !dst->render || dstW <= 0 || dstH_ <= 0 || w <= 0 || h <= 0)
		return;
	ensureFrame();

	/* make the EFB writes visible to texture fetches */
	GX2Invalidate(GX2_INVALIDATE_MODE_COLOR_BUFFER, efbCb.surface.image, efbCb.surface.imageSize);
	GX2Invalidate(GX2_INVALIDATE_MODE_TEXTURE, efbCb.surface.image, efbCb.surface.imageSize);

	GX2SetColorBuffer(&dst->cb, GX2_RENDER_TARGET_0);

	const float fw = (float) efbW, fh = (float) efbH;
	const float u0 = (float) (x * scale) / fw, v0 = (float) (y * scale) / fh;
	const float u1 = (float) ((x + w) * scale) / fw, v1 = (float) ((y + h) * scale) / fh;
	Vertex q[4];
	memset(q, 0, sizeof(q));
	const float xs[4] = { -1, 1, 1, -1 }, ys[4] = { 1, 1, -1, -1 };
	const float us[4] = { u0, u1, u1, u0 }, vs_[4] = { v0, v0, v1, v1 };
	for (int i = 0; i < 4; i++) {
		q[i].x = xs[i]; q[i].y = ys[i];
		q[i].r = q[i].g = q[i].b = q[i].a = 255;
		q[i].s0 = us[i]; q[i].t0 = vs_[i];
	}
	TevState tev;
	tevReset(tev);                      /* stage 0: REPLACE texmap 0 */
	tev.stage[0].texmap = GX_TEXMAP0;
	tev.stage[0].color = GX_COLOR_NULL;
	PsFixedParams fp;
	memset(&fp, 0, sizeof(fp));
	static float psBuf[kPsUniformVec4 * 4];
	DrawCall dc;
	memset(&dc, 0, sizeof(dc));
	makeQuadCall(&dc, q, flatPipeline(dstW, dstH_, 15), psBuf, tev, fp);
	dc.tex[0].tex = (TexHandle) 0;
	GpuTex efbRef;
	memset(&efbRef, 0, sizeof(efbRef));
	efbRef.tex = efbTex;
	dc.tex[0].tex = &efbRef;
	dc.tex[0].sampler.wrapS = dc.tex[0].sampler.wrapT = GX_CLAMP;
	const bool exact = (w * scale == dstW && h * scale == dstH_);
	dc.tex[0].sampler.minLinear = dc.tex[0].sampler.magLinear = exact ? 0 : 1;
	dc.tex[0].compMap = 0x00010203;
	dc.tex[1].tex = 0;

	const int s = scale;
	scale = 1;
	issue(dc, false, dstW, dstH_);
	scale = s;

	GX2Invalidate(GX2_INVALIDATE_MODE_COLOR_BUFFER, dst->cb.surface.image, dst->cb.surface.imageSize);
	GX2Invalidate(GX2_INVALIDATE_MODE_TEXTURE, dst->cb.surface.image, dst->cb.surface.imageSize);

	bindEfb();
	forceStateReload();
}

void Backend2::present(int widthPx, int heightPx)
{
	ensureFrame();
	/*
	 * The EFB is cleared right after GX_CopyDisp() and reused for the next frame, so the app gets a
	 * private copy (two buffers alternate: a frame stays valid until the second following present).
	 */
	GpuTex *pt = presentTex[presentParity];
	presentParity ^= 1;
	copyEfbToTexture(pt, efbW, efbH, 0, 0, kBaseW, kBaseH);
	GX2Flush();
	inFrame = false;
	frame++;
	releaseRetired(false);
	Wiiu_PresentEmuFrame(&pt->tex, widthPx > efbW ? efbW : widthPx, heightPx > efbH ? efbH : heightPx);
}

TexHandle Backend2::createTexture(int w, int h, const uint8_t *rgba)
{
	if (w <= 0 || h <= 0 || !rgba)
		return 0;
	GpuTex *t = new GpuTex();
	memset(t, 0, sizeof(*t));
	GX2InitTexture(&t->tex, (uint32_t) w, (uint32_t) h, 1, 0, GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8, GX2_SURFACE_DIM_TEXTURE_2D, GX2_TILE_MODE_LINEAR_ALIGNED);
	t->tex.surface.image = MEMAllocFromDefaultHeapEx(t->tex.surface.imageSize, t->tex.surface.alignment);
	if (!t->tex.surface.image) {
		delete t;
		return 0;
	}
	uint8_t *d = (uint8_t *) t->tex.surface.image;
	const uint32_t stride = t->tex.surface.pitch * 4;
	for (int y = 0; y < h; y++)
		memcpy(d + (size_t) y * stride, rgba + (size_t) y * w * 4, (size_t) w * 4);
	GX2InitTextureRegs(&t->tex);   /* the register block holds the image address */
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, t->tex.surface.image, t->tex.surface.imageSize);
	texCount++;
	return t;
}

TexHandle Backend2::createRenderTexture(int w, int h)
{
	if (w <= 0 || h <= 0)
		return 0;
	GpuTex *t = new GpuTex();
	memset(t, 0, sizeof(*t));
	t->render = 1;
	if (!makeSurface(&t->cb.surface, w, h, (GX2SurfaceUse) (GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER),
	                 GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8)) {
		delete t;
		return 0;
	}
	t->cb.viewNumSlices = 1;
	GX2InitColorBufferRegs(&t->cb);
	texViewOf(&t->tex, t->cb.surface);
	texCount++;
	return t;
}

void Backend2::destroyTexture(TexHandle h)
{
	GpuTex *t = (GpuTex *) h;
	if (!t)
		return;
	t->retireFrame = frame;
	retired.push_back(t);   /* the GPU may still be using it: freed kFramesBeforeFree presents later */
}

void Backend2::releaseRetired(bool all)
{
	size_t keep = 0;
	for (size_t i = 0; i < retired.size(); i++) {
		GpuTex *t = retired[i];
		if (all || frame - t->retireFrame >= kFramesBeforeFree) {
			void *img = t->render ? t->cb.surface.image : t->tex.surface.image;
			if (img)
				MEMFreeToDefaultHeap(img);
			delete t;
			if (texCount)
				texCount--;
		} else {
			retired[keep++] = t;
		}
	}
	retired.resize(keep);
}

} /* namespace */

/* ------------------------------------------------------------------------ */

extern "C" __attribute__((weak)) void Wiiu_PresentEmuFrame(struct GX2Texture *colour, int width, int height)
{
	(void) colour; (void) width; (void) height;
}

/* Defaults for the video-mode globals glN64_GX reads; the app may provide its own. */
static GXRModeObj sMode = { 0, 640, 480, 480, 0, 0, 640, 480, 0, 0, 0, { { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 }, { 6, 6 } },
                            { 0, 0, 21, 22, 21, 0, 0 }, 0 };
__attribute__((weak)) GXRModeObj *rmode = &sMode;
__attribute__((weak)) GXRModeObj *vmode = &sMode;
__attribute__((weak)) int GX_xfb_offset = 0;

namespace gx2gx {

bool init(int scale)
{
	if (gBackend)
		return true;
	gBackend = new Backend2();
	if (!gBackend->setup(scale < 1 ? 1 : (scale > 4 ? 4 : scale))) {
		gBackend->teardown();
		delete gBackend;
		gBackend = 0;
		return false;
	}
	coreSetBackend(gBackend, scale < 1 ? 1 : (scale > 4 ? 4 : scale));
	return true;
}

void shutdown()
{
	if (!gBackend)
		return;
	GX2DrawDone();
	coreSetBackend(0, 1);
	gBackend->teardown();
	delete gBackend;
	gBackend = 0;
}

} /* namespace gx2gx */
