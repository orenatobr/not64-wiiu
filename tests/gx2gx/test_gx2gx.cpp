/*
 * Host-side unit tests for the pure logic of the GX2 renderer: TEV packing and
 * evaluation, texture detiling, GX state to draw-packet translation. Built and run by tests/gx2gx/run.sh.
 * Expected values are computed by hand from the libogc documentation (see comments).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include "../../wiiu/source/gx2gx/Gx2Backend.h"
#include "../../wiiu/source/gx2gx/Gx2Gx.h"
#include "../../wiiu/source/gx2gx/Gx2Map.h"
#include "../../wiiu/source/gx2gx/Gx2Tev.h"
#include "../../wiiu/source/gx2gx/Gx2Tex.h"
#include "../../wiiu/source/gx2gx/compat/gccore.h"

using namespace gx2gx;

static int gFail = 0, gChecks = 0;

#define CHECK(cond) do { gChecks++; if (!(cond)) { gFail++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_NEAR(a, b, eps) do { gChecks++; double _a = (a), _b = (b); if (fabs(_a - _b) > (eps)) { gFail++; printf("FAIL %s:%d: %s = %g, expected %g\n", __FILE__, __LINE__, #a, _a, _b); } } while (0)

/* ---------------------------------------------------------------- mock backend */
struct MockBackend : Backend {
	std::vector<Vertex> verts;
	DrawCall last;
	std::vector<float> lastPs;
	int draws, texCreated, texDestroyed, renderTex, presents, clears, copies;
	float lastClear[4]; unsigned lastMask; bool lastClearDepth; float lastDepth;
	int presentW, presentH;
	MockBackend() { reset(); }
	void reset() { draws = texCreated = texDestroyed = renderTex = presents = clears = copies = 0; verts.clear(); presentW = presentH = 0; }
	TexHandle createTexture(int, int, const uint8_t *) { texCreated++; return (TexHandle) (uintptr_t) (0x1000 + texCreated); }
	TexHandle createRenderTexture(int, int) { renderTex++; return (TexHandle) (uintptr_t) (0x9000 + renderTex); }
	void destroyTexture(TexHandle) { texDestroyed++; }
	void draw(const DrawCall &dc) {
		draws++; last = dc; verts.assign(dc.verts, dc.verts + dc.count); last.verts = &verts[0];
		lastPs.assign(dc.ps, dc.ps + kPsUniformVec4 * 4); last.ps = &lastPs[0];
	}
	void clearEfb(const float c[4], unsigned m, bool d, float z) { clears++; memcpy(lastClear, c, sizeof(lastClear)); lastMask = m; lastClearDepth = d; lastDepth = z; }
	void copyEfbToTexture(TexHandle, int, int, int, int, int, int) { copies++; }
	void present(int w, int h) { presents++; presentW = w; presentH = h; }
	void drawDone() {}
	void flush() {}
};

static Rgba rgba(float r, float g, float b, float a) { Rgba x; x.c[0] = r; x.c[1] = g; x.c[2] = b; x.c[3] = a; return x; }

static void checkRgba(const Rgba &v, float r, float g, float b, float a, float eps = 1e-5f)
{
	CHECK_NEAR(v.c[0], r, eps); CHECK_NEAR(v.c[1], g, eps); CHECK_NEAR(v.c[2], b, eps); CHECK_NEAR(v.c[3], a, eps);
}

/* ---------------------------------------------------------------- TEV */
static void testTevHandVectors()
{
	TevState t; tevReset(t);
	Rgba ras = rgba(1.0f, 0.5f, 0.5f, 1.0f);
	Rgba tex[2] = { rgba(0.5f, 0.25f, 1.0f, 0.5f), rgba(0, 0, 0, 0) };
	PsFixedParams fp; memset(&fp, 0, sizeof(fp));
	float ps[kPsUniformVec4 * 4];

	/* GX_MODULATE: Cv = Cr*Ct, Av = Ar*At (libogc docs) */
	t.numStages = 1;
	t.stage[0].texmap = GX_TEXMAP0; t.stage[0].color = GX_COLOR0A0;
	tevSetOp(t, 0, GX_MODULATE);
	checkRgba(tevEvaluate(t, ras, tex), 0.5f, 0.125f, 0.5f, 0.5f);
	int n = tevPack(t, fp, ps);
	CHECK(n == 1);
	checkRgba(tevEvaluatePacked(ps, n, ras, tex), 0.5f, 0.125f, 0.5f, 0.5f);

	/* GX_REPLACE: Cv = Ct */
	tevSetOp(t, 0, GX_REPLACE);
	checkRgba(tevEvaluate(t, ras, tex), 0.5f, 0.25f, 1.0f, 0.5f);

	/* GX_DECAL: Cv = (1-At)*Cr + At*Ct ; Av = Ar  => (0.5*1 + 0.5*0.5, 0.5*0.5+0.5*0.25, 0.5*0.5+0.5*1) */
	tevSetOp(t, 0, GX_DECAL);
	checkRgba(tevEvaluate(t, ras, tex), 0.75f, 0.375f, 0.75f, 1.0f);

	/* GX_BLEND: Cv = (1-Ct)*Cr + Ct ; Av = At*Ar */
	tevSetOp(t, 0, GX_BLEND);
	checkRgba(tevEvaluate(t, ras, tex), 0.5f * 1.0f + 0.5f, 0.75f * 0.5f + 0.25f, 0.0f + 1.0f, 0.5f);

	/* lerp of two registers with c = HALF: (1-.5)*C0 + .5*C1 */
	tevReset(t);
	t.reg[1][0] = 1.0f; t.reg[1][3] = 1.0f;     /* reg0 = (1,0,0,1) */
	t.reg[2][2] = 1.0f; t.reg[2][3] = 0.0f;     /* reg1 = (0,0,1,0) */
	t.stage[0].texmap = GX_TEXMAP_NULL; t.stage[0].color = GX_COLOR_NULL;
	GX_SetNumTevStages(1);
	t.stage[0].cIn[0] = GX_CC_C0; t.stage[0].cIn[1] = GX_CC_C1; t.stage[0].cIn[2] = GX_CC_HALF; t.stage[0].cIn[3] = GX_CC_ZERO;
	t.stage[0].aIn[0] = GX_CA_A0; t.stage[0].aIn[1] = GX_CA_A1; t.stage[0].aIn[2] = GX_CA_KONST; t.stage[0].aIn[3] = GX_CA_ZERO;
	t.stage[0].kaSel = GX_TEV_KASEL_1_2;
	checkRgba(tevEvaluate(t, ras, tex), 0.5f, 0.0f, 0.5f, 0.5f);
	n = tevPack(t, fp, ps);
	checkRgba(tevEvaluatePacked(ps, n, ras, tex), 0.5f, 0.0f, 0.5f, 0.5f);

	/* SUB: d - ((1-c)a + cb) with d=ONE, a=KONST(K0 = 0.25), b=c=ZERO: 1 - 0.25 */
	tevReset(t);
	t.kcolor[0][0] = t.kcolor[0][1] = t.kcolor[0][2] = 0.25f;
	t.stage[0].texmap = GX_TEXMAP_NULL; t.stage[0].color = GX_COLOR_NULL;
	t.stage[0].cIn[0] = GX_CC_KONST; t.stage[0].cIn[1] = GX_CC_ZERO; t.stage[0].cIn[2] = GX_CC_ZERO; t.stage[0].cIn[3] = GX_CC_ONE;
	t.stage[0].kcSel = GX_TEV_KCSEL_K0;
	t.stage[0].cOp = GX_TEV_SUB;
	checkRgba(tevEvaluate(t, ras, tex), 0.75f, 0.75f, 0.75f, 0.0f);
	n = tevPack(t, fp, ps);
	checkRgba(tevEvaluatePacked(ps, n, ras, tex), 0.75f, 0.75f, 0.75f, 0.0f);

	/* bias +0.5 then scale 2 with clamp: (0.1+0.5)*2 = 1.2 -> 1.0 ; unclamped stays 1.2 */
	tevReset(t);
	t.kcolor[1][0] = t.kcolor[1][1] = t.kcolor[1][2] = 0.1f;
	t.stage[0].texmap = GX_TEXMAP_NULL; t.stage[0].color = GX_COLOR_NULL;
	t.stage[0].cIn[0] = t.stage[0].cIn[1] = t.stage[0].cIn[2] = GX_CC_ZERO; t.stage[0].cIn[3] = GX_CC_KONST;
	t.stage[0].kcSel = GX_TEV_KCSEL_K1;
	t.stage[0].cBias = GX_TB_ADDHALF; t.stage[0].cScale = GX_CS_SCALE_2;
	checkRgba(tevEvaluate(t, ras, tex), 1.0f, 1.0f, 1.0f, 0.0f);
	t.stage[0].cClamp = GX_FALSE;
	checkRgba(tevEvaluate(t, ras, tex), 1.2f, 1.2f, 1.2f, 0.0f);
	n = tevPack(t, fp, ps);
	checkRgba(tevEvaluatePacked(ps, n, ras, tex), 1.2f, 1.2f, 1.2f, 0.0f);

	/* two stages through REG0: stage0 RASC -> REG0 (alpha stays in PREV), stage1 d = C0 */
	tevReset(t);
	t.numStages = 2;
	t.stage[0].texmap = GX_TEXMAP_NULL; t.stage[0].color = GX_COLOR0A0;
	tevSetOp(t, 0, GX_PASSCLR);
	t.stage[0].cOut = GX_TEVREG0;
	t.stage[1].texmap = GX_TEXMAP_NULL; t.stage[1].color = GX_COLOR_NULL;
	t.stage[1].cIn[0] = t.stage[1].cIn[1] = t.stage[1].cIn[2] = GX_CC_ZERO; t.stage[1].cIn[3] = GX_CC_C0;
	t.stage[1].aIn[3] = GX_CA_ZERO;
	checkRgba(tevEvaluate(t, ras, tex), 1.0f, 0.5f, 0.5f, 0.0f);
	n = tevPack(t, fp, ps);
	CHECK(n == 2);
	checkRgba(tevEvaluatePacked(ps, n, ras, tex), 1.0f, 0.5f, 0.5f, 0.0f);

	/* texmap NULL gives TEXC = 0, so MODULATE yields black */
	tevReset(t);
	t.stage[0].texmap = GX_TEXMAP_NULL; t.stage[0].color = GX_COLOR0A0;
	tevSetOp(t, 0, GX_MODULATE);
	checkRgba(tevEvaluate(t, ras, tex), 0, 0, 0, 0);
}

static void testTevPassThroughDrop()
{
	/* glN64 appends a PASSCLR stage for the Z texture; it must not cost a shader stage */
	TevState t; tevReset(t);
	t.numStages = 3;
	t.stage[0].texmap = GX_TEXMAP0; t.stage[0].color = GX_COLOR0A0; tevSetOp(t, 0, GX_MODULATE);
	t.stage[1].texmap = GX_TEXMAP1; t.stage[1].color = GX_COLOR0A0; tevSetOp(t, 1, GX_MODULATE);
	tevSetOp(t, 2, GX_PASSCLR);
	CHECK(tevEffectiveStages(t) == 2);
	t.numStages = 16;
	for (int i = 0; i < 16; i++) { t.stage[i].texmap = GX_TEXMAP0; t.stage[i].color = GX_COLOR0A0; tevSetOp(t, i, GX_MODULATE); }
	CHECK(tevEffectiveStages(t) == kShaderTevStages);   /* truncated, documented */
}

static float frand() { return (float) rand() / (float) RAND_MAX; }

static void testTevPackedMatchesReference()
{
	srand(12345);
	int mismatches = 0;
	for (int iter = 0; iter < 4000; iter++) {
		TevState t; tevReset(t);
		t.numStages = (uint8_t) (1 + rand() % 7);
		for (int r = 1; r < 4; r++) for (int k = 0; k < 4; k++) t.reg[r][k] = frand();
		for (int r = 0; r < 4; r++) for (int k = 0; k < 4; k++) t.kcolor[r][k] = frand();
		for (int s = 0; s < t.numStages; s++) {
			TevStage &st = t.stage[s];
			st.texmap = (uint8_t) (rand() % 3 == 0 ? GX_TEXMAP_NULL : rand() % 4);
			st.color = (uint8_t) (rand() % 4 == 0 ? GX_COLOR_NULL : GX_COLOR0A0);
			for (int i = 0; i < 4; i++) { st.cIn[i] = (uint8_t) (rand() % 16); st.aIn[i] = (uint8_t) (rand() % 8); }
			st.cOp = (uint8_t) (rand() % 2); st.aOp = (uint8_t) (rand() % 2);
			st.cBias = (uint8_t) (rand() % 3); st.aBias = (uint8_t) (rand() % 3);
			st.cScale = (uint8_t) (rand() % 4); st.aScale = (uint8_t) (rand() % 4);
			st.cClamp = (uint8_t) (rand() % 2); st.aClamp = (uint8_t) (rand() % 2);
			st.cOut = (uint8_t) (rand() % 4); st.aOut = (uint8_t) (rand() % 4);
			st.kcSel = (uint8_t) (rand() % 2 ? rand() % 8 : 0x0C + rand() % 20);
			st.kaSel = (uint8_t) (rand() % 2 ? rand() % 8 : 0x10 + rand() % 16);
		}
		Rgba ras = rgba(frand(), frand(), frand(), frand());
		Rgba tex[2] = { rgba(frand(), frand(), frand(), frand()), rgba(frand(), frand(), frand(), frand()) };
		PsFixedParams fp; memset(&fp, 0, sizeof(fp));
		float ps[kPsUniformVec4 * 4];
		int n = tevPack(t, fp, ps);
		Rgba a = tevEvaluate(t, ras, tex), b = tevEvaluatePacked(ps, n, ras, tex);
		for (int k = 0; k < 4; k++)
			if (fabsf(a.c[k] - b.c[k]) > 2e-4f)
				mismatches++;
	}
	CHECK(mismatches == 0);
}

static void testKonstSelection()
{
	TevState t; tevReset(t);
	t.kcolor[2][0] = 0.1f; t.kcolor[2][1] = 0.2f; t.kcolor[2][2] = 0.3f; t.kcolor[2][3] = 0.4f;
	float c[3];
	tevKonstColor(t, GX_TEV_KCSEL_K2, c);
	CHECK_NEAR(c[0], 0.1, 1e-6); CHECK_NEAR(c[2], 0.3, 1e-6);
	tevKonstColor(t, GX_TEV_KCSEL_K2_G, c);
	CHECK_NEAR(c[0], 0.2, 1e-6); CHECK_NEAR(c[1], 0.2, 1e-6);
	tevKonstColor(t, GX_TEV_KCSEL_3_4, c);
	CHECK_NEAR(c[0], 0.75, 1e-6);
	CHECK_NEAR(tevKonstAlpha(t, GX_TEV_KASEL_K2_A), 0.4, 1e-6);
	CHECK_NEAR(tevKonstAlpha(t, GX_TEV_KASEL_1_4), 0.25, 1e-6);
}

static void testSwapCompMap()
{
	TevState t; tevReset(t);
	CHECK(tevTexCompMap(t, 0) == 0x00010203u);
	t.stage[0].texSwap = GX_TEV_SWAP1;               /* default table 1 = RRRA */
	CHECK(tevTexCompMap(t, 0) == 0x00000003u);
	t.swap[2][0] = GX_CH_ALPHA; t.swap[2][1] = GX_CH_BLUE; t.swap[2][2] = GX_CH_GREEN; t.swap[2][3] = GX_CH_RED;
	t.stage[0].texSwap = GX_TEV_SWAP2;
	CHECK(tevTexCompMap(t, 0) == 0x03020100u);
}

/* ---------------------------------------------------------------- textures */
static void testTexDecode()
{
	uint8_t out[8 * 8 * 4];
	/* RGBA8 4x4: texel (x,y) -> AR pair at 2*(y*4+x), GB pair at 32+2*(y*4+x) */
	uint8_t blk[64]; memset(blk, 0, sizeof(blk));
	for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
		int i = (y * 4 + x) * 2;
		blk[i] = (uint8_t) (0xA0 + y); blk[i + 1] = (uint8_t) (0x10 + x);       /* A, R */
		blk[32 + i] = (uint8_t) (0x20 + x); blk[32 + i + 1] = (uint8_t) (0x30 + y); /* G, B */
	}
	CHECK(texDecode(GX_TF_RGBA8, 4, 4, blk, sizeof(blk), out));
	CHECK(out[(2 * 4 + 3) * 4 + 0] == 0x13 && out[(2 * 4 + 3) * 4 + 1] == 0x23 && out[(2 * 4 + 3) * 4 + 2] == 0x32 && out[(2 * 4 + 3) * 4 + 3] == 0xA2);

	/* RGB565 8x4: two 4x4 blocks side by side; second block texel (1,2) */
	uint8_t d565[64]; memset(d565, 0, sizeof(d565));
	d565[32 + (2 * 4 + 1) * 2] = 0xF8; d565[32 + (2 * 4 + 1) * 2 + 1] = 0x00;     /* pure red */
	CHECK(texDecode(GX_TF_RGB565, 8, 4, d565, sizeof(d565), out));
	CHECK(out[(2 * 8 + 5) * 4 + 0] == 255 && out[(2 * 8 + 5) * 4 + 1] == 0 && out[(2 * 8 + 5) * 4 + 3] == 255);

	/* RGB5A3: opaque white, then 3-bit alpha 7 with 0xF0F0 colour => a = 255, r=255,g=0,b=255... */
	uint8_t d5a3[32]; memset(d5a3, 0, sizeof(d5a3));
	d5a3[0] = 0xFF; d5a3[1] = 0xFF;       /* texel (0,0) opaque white */
	d5a3[2] = 0x7F; d5a3[3] = 0x0F;       /* texel (1,0): a3=7, r=15, g=0, b=15 */
	d5a3[4] = 0x20; d5a3[5] = 0x80;       /* texel (2,0): a3=2 => 0x49 (73), r=0,g=8,b=0 */
	CHECK(texDecode(GX_TF_RGB5A3, 4, 4, d5a3, sizeof(d5a3), out));
	CHECK(out[0] == 255 && out[1] == 255 && out[2] == 255 && out[3] == 255);
	CHECK(out[4] == 255 && out[5] == 0 && out[6] == 255 && out[7] == 255);
	CHECK(out[8] == 0 && out[9] == 136 && out[10] == 0 && out[11] == 73);

	/* IA4 8x4: byte = alpha<<4 | intensity; texel (7,3) is the last byte of the block */
	uint8_t ia4[32]; memset(ia4, 0, sizeof(ia4)); ia4[31] = 0x8F;
	CHECK(texDecode(GX_TF_IA4, 8, 4, ia4, sizeof(ia4), out));
	CHECK(out[(3 * 8 + 7) * 4 + 0] == 255 && out[(3 * 8 + 7) * 4 + 3] == 136);

	/* I4 8x8: high nibble is the left texel; texel (1,0) = low nibble of byte 0 */
	uint8_t i4[32]; memset(i4, 0, sizeof(i4)); i4[0] = 0x3A;
	CHECK(texDecode(GX_TF_I4, 8, 8, i4, sizeof(i4), out));
	CHECK(out[0] == 0x33 && out[3] == 0x33 && out[4] == 0xAA);

	/* IA8: alpha byte first */
	uint8_t ia8[32]; memset(ia8, 0, sizeof(ia8)); ia8[0] = 0x40; ia8[1] = 0x90;
	CHECK(texDecode(GX_TF_IA8, 4, 4, ia8, sizeof(ia8), out));
	CHECK(out[0] == 0x90 && out[3] == 0x40);

	/* non block-multiple size: 6x3 RGB565 needs 2x1 blocks = 64 bytes; 63 bytes must be rejected */
	uint8_t big[64]; memset(big, 0, sizeof(big));
	CHECK(texTiledBytes(GX_TF_RGB565, 6, 3) == 64);
	CHECK(texDecode(GX_TF_RGB565, 6, 3, big, 64, out));
	CHECK(!texDecode(GX_TF_RGB565, 6, 3, big, 63, out));
	CHECK(!texDecode(GX_TF_CMPR, 8, 8, big, 64, out));
	CHECK(!texDecode(GX_TF_RGB565, 0, 3, big, 64, out));
	CHECK(!texDecode(GX_TF_RGB565, 100000, 3, big, 64, out));
}

/* ---------------------------------------------------------------- core / vertex path */
static void beginSimple(u8 prim, int n)
{
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX1, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX2, GX_DIRECT);
	GX_Begin(prim, GX_VTXFMT0, (u16) n);
}

static void vtx(float x, float y, float z, u8 r, float s0, float t0, float s1, float t1)
{
	GX_Position3f32(x, y, z);
	GX_Color4u8(r, 2, 3, 4);
	GX_TexCoord2f32(s0, t0);
	GX_TexCoord2f32(s1, t1);
	GX_TexCoord2f32(9.0f, 9.0f);
}

static void testVertexAssembly(MockBackend &mb)
{
	mb.reset();
	GX_SetTevOrder(0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GX_SetTevOrder(1, GX_TEXCOORD1, GX_TEXMAP1, GX_COLOR0A0);
	beginSimple(GX_TRIANGLES, 3);
	vtx(1, 2, 3, 10, 0.1f, 0.2f, 0.3f, 0.4f);
	vtx(4, 5, 6, 11, 0.5f, 0.6f, 0.7f, 0.8f);
	vtx(7, 8, 9, 12, 0.9f, 1.0f, 1.1f, 1.2f);
	GX_End();
	CHECK(mb.draws == 1 && mb.last.prim == PRIM_TRIANGLES && mb.last.count == 3);
	CHECK(mb.verts.size() == 3);
	CHECK(mb.verts[1].x == 4 && mb.verts[1].y == 5 && mb.verts[1].z == 6);
	CHECK(mb.verts[1].r == 11 && mb.verts[1].g == 2 && mb.verts[1].b == 3 && mb.verts[1].a == 4);
	CHECK_NEAR(mb.verts[2].s0, 0.9, 1e-6); CHECK_NEAR(mb.verts[2].t1, 1.2, 1e-6);

	/* extra vertices beyond vtxcnt are dropped, incomplete primitive is truncated to a whole triangle */
	mb.reset();
	beginSimple(GX_TRIANGLES, 4);
	for (int i = 0; i < 6; i++) vtx((float) i, 0, 0, 1, 0, 0, 0, 0);
	GX_End();
	CHECK(mb.draws == 1 && mb.last.count == 3);

	/* nothing drawn for an empty or incomplete primitive */
	mb.reset();
	beginSimple(GX_QUADS, 4);
	vtx(0, 0, 0, 1, 0, 0, 0, 0); vtx(1, 0, 0, 1, 0, 0, 0, 0);
	GX_End();
	CHECK(mb.draws == 0);

	/* position-only vertices (VI.cpp style): white colour, 2D position, zero z */
	mb.reset();
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_Begin(GX_QUADS, GX_VTXFMT1, 4);
	GX_Position2f32(0, 0); GX_Position2f32(10, 0); GX_Position2f32(10, 10); GX_Position2f32(0, 10);
	GX_End();
	CHECK(mb.draws == 1 && mb.last.prim == PRIM_QUADS && mb.verts[2].x == 10 && mb.verts[2].z == 0 && mb.verts[2].r == 255);

	/* texgen: texmap1 sampled through TEXCOORD0 -> attribute TEX2 */
	mb.reset();
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX2, GX_IDENTITY);
	GX_SetTevOrder(0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GX_SetTevOrder(1, GX_TEXCOORD0, GX_TEXMAP1, GX_COLOR0A0);
	GX_SetNumTevStages(2);
	beginSimple(GX_QUADS, 4);
	for (int i = 0; i < 4; i++) vtx((float) i, 0, 0, 1, 0, 0, 0, 0);
	GX_End();
	CHECK(mb.draws == 1);
	CHECK_NEAR(mb.verts[0].s1, 9.0, 1e-6);
	/* restore */
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
}

static void testMatricesAndDepth(MockBackend &mb)
{
	/* glN64's combW projection: [2][2]=0 [2][3]=-0.5 [3][2]=-1, vertices are (x, y, -w) */
	Mtx44 p; guMtx44Identity(p);
	p[2][2] = 0.0f; p[2][3] = -0.5f; p[3][2] = -1.0f; p[3][3] = 0.0f;
	Mtx m; guMtxIdentity(m);
	GX_LoadProjectionMtx(p, GX_PERSPECTIVE);
	GX_LoadPosMtxImm(m, GX_PNMTX0);
	GX_SetViewport(0, 0, 640, 480, 0.0f, 1.0f);
	GX_SetZTexture(GX_ZT_DISABLE, GX_TF_Z8, 0);
	mb.reset();
	beginSimple(GX_TRIANGLES, 3);
	for (int i = 0; i < 3; i++) vtx(2.0f, 3.0f, -4.0f, 1, 0, 0, 0, 0);
	GX_End();
	const VsParams &v = mb.last.vs;
	float in[4] = { 2.0f, 3.0f, -4.0f, 1.0f }, clip[4];
	for (int r = 0; r < 4; r++) clip[r] = in[0] * v.col[0][r] + in[1] * v.col[1][r] + in[2] * v.col[2][r] + in[3] * v.col[3][r];
	CHECK_NEAR(clip[0], 2.0, 1e-6); CHECK_NEAR(clip[1], 3.0, 1e-6);
	CHECK_NEAR(clip[2], -0.5, 1e-6); CHECK_NEAR(clip[3], 4.0, 1e-6);          /* w = -z = 4 */
	/* GX depth: ndc = -0.5/4 ; z = far + ndc*(far-near) with near=0 far=1  =>  0.875; shader: z_out/w */
	float zout = clip[2] * v.zScale + clip[3] * v.zOffset;
	CHECK_NEAR(zout / clip[3], 1.0 - 0.125, 1e-6);

	/* orthographic: only the elements the XF unit reads are used (row 3 ignored) */
	guOrtho(p, 0, 480, 0, 640, 0.0f, 1.0f);
	p[3][0] = 99.0f;
	GX_LoadProjectionMtx(p, GX_ORTHOGRAPHIC);
	GX_SetViewport(0, 0, 640, 480, 0.25f, 0.75f);
	mb.reset();
	beginSimple(GX_QUADS, 4);
	for (int i = 0; i < 4; i++) vtx(320.0f, 240.0f, 0.5f, 1, 0, 0, 0, 0);
	GX_End();
	const VsParams &o = mb.last.vs;
	float in2[4] = { 320.0f, 240.0f, 0.5f, 1.0f }, c2[4];
	for (int r = 0; r < 4; r++) c2[r] = in2[0] * o.col[0][r] + in2[1] * o.col[1][r] + in2[2] * o.col[2][r] + in2[3] * o.col[3][r];
	CHECK_NEAR(c2[0], 0.0, 1e-5); CHECK_NEAR(c2[1], 0.0, 1e-5); CHECK_NEAR(c2[3], 1.0, 1e-6);
	/* guOrtho z row: -1/(f-n)*z - f/(f-n) = -0.5 - 1 */
	CHECK_NEAR(c2[2], -1.5, 1e-6);
	float depth = (c2[2] * o.zScale + c2[3] * o.zOffset) / c2[3];
	CHECK_NEAR(depth, 0.75 + (-1.5) * (0.75 - 0.25), 1e-6);

	/* Z texture: REPLACE gives a constant depth, ADD shifts it */
	GX_SetZTexture(GX_ZT_REPLACE, GX_TF_Z24X8, GX_MAX_Z24 / 2);
	mb.reset();
	beginSimple(GX_QUADS, 4);
	for (int i = 0; i < 4; i++) vtx(0, 0, 0, 1, 0, 0, 0, 0);
	GX_End();
	CHECK_NEAR(mb.last.vs.zScale, 0.0, 1e-9);
	CHECK_NEAR(mb.last.vs.zOffset, (double) (GX_MAX_Z24 / 2) / GX_MAX_Z24, 1e-6);
	GX_SetZTexture(GX_ZT_DISABLE, GX_TF_Z8, 0);
}

static void testStateMapping(MockBackend &mb)
{
	/* alpha compare: single comparison is carried to the hardware test with the 8-bit half-step */
	GX_SetAlphaCompare(GX_GEQUAL, 128, GX_AOP_AND, GX_ALWAYS, 0);
	mb.reset();
	beginSimple(GX_TRIANGLES, 3);
	for (int i = 0; i < 3; i++) vtx(0, 0, 0, 1, 0, 0, 0, 0);
	GX_End();
	CHECK(mb.last.pipe.alphaTest.enable == 1 && mb.last.pipe.alphaTest.func == GX_GEQUAL);
	CHECK_NEAR(mb.last.pipe.alphaTest.ref, 127.5 / 255.0, 1e-6);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	mb.reset();
	beginSimple(GX_TRIANGLES, 3);
	for (int i = 0; i < 3; i++) vtx(0, 0, 0, 1, 0, 0, 0, 0);
	GX_End();
	CHECK(mb.last.pipe.alphaTest.enable == 0);

	/* pipeline state is carried verbatim */
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	GX_SetZMode(GX_ENABLE, GX_LEQUAL, GX_TRUE);
	GX_SetCullMode(GX_CULL_FRONT);
	GX_SetScissor(1, 2, 3, 4);
	GX_SetLineWidth(12, GX_TO_ZERO);
	GX_SetColorUpdate(GX_TRUE); GX_SetAlphaUpdate(GX_FALSE);
	mb.reset();
	beginSimple(GX_LINES, 2);
	for (int i = 0; i < 2; i++) vtx(0, 0, 0, 1, 0, 0, 0, 0);
	GX_End();
	const PipelineState &p = mb.last.pipe;
	CHECK(mb.last.prim == PRIM_LINES);
	CHECK(p.blend.type == GX_BM_BLEND && p.blend.src == GX_BL_SRCALPHA && p.blend.dst == GX_BL_INVSRCALPHA);
	CHECK(p.zEnable && p.zFunc == GX_LEQUAL && p.zUpdate);
	CHECK(p.cull == GX_CULL_FRONT);
	CHECK(p.scissor[0] == 1 && p.scissor[1] == 2 && p.scissor[2] == 3 && p.scissor[3] == 4);
	CHECK_NEAR(p.lineWidth, 2.0, 1e-6);
	CHECK(p.colorUpdate == 1 && p.alphaUpdate == 0);
	GX_SetAlphaUpdate(GX_TRUE);

	/* fog packs into the uniforms: N64 start/end in [-1,1] z, near/far -1..1 */
	GX_SetFog(GX_FOG_ORTHO_LIN, -1.0f, 1.0f, -1.0f, 1.0f, (GXColor) { 10, 20, 30, 255 });
	mb.reset();
	beginSimple(GX_TRIANGLES, 3);
	for (int i = 0; i < 3; i++) vtx(0, 0, 0, 1, 0, 0, 0, 0);
	GX_End();
	CHECK_NEAR(mb.last.ps[3 * 4 + 2], 1.0, 1e-9);
	/* zscreen 0 -> ze = -1 -> f = 0 ; zscreen 1 -> ze = 1 -> f = 1 */
	float A = mb.last.ps[3 * 4 + 0], B = mb.last.ps[3 * 4 + 1];
	CHECK_NEAR(0.0f * A + B, 0.0, 1e-6); CHECK_NEAR(1.0f * A + B, 1.0, 1e-6);
	CHECK_NEAR(mb.last.ps[4 * 4 + 1], 20.0 / 255.0, 1e-6);
	GX_SetFog(GX_FOG_NONE, 0, 1, 0, 1, (GXColor) { 0, 0, 0, 255 });
}

static void testTextureCache(MockBackend &mb)
{
	heap_cntrl heap;
	__lwp_heap_init(&heap, 0, 4096, 32);
	void *a = __lwp_heap_allocate(&heap, 2048);
	void *b = __lwp_heap_allocate(&heap, 2048);
	void *c = __lwp_heap_allocate(&heap, 32);
	CHECK(a && b && !c);                     /* budget exhausted, like the fixed MEM2 cache */
	CHECK(heap.used == 4096);
	memset(a, 0, 2048);

	GXTexObj obj;
	GX_InitTexObj(&obj, a, 16, 16, GX_TF_RGB5A3, GX_CLAMP, GX_REPEAT, GX_FALSE);
	GX_InitTexObjLOD(&obj, GX_NEAR, GX_LINEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
	GX_LoadTexObj(&obj, GX_TEXMAP0);
	GX_SetTevOrder(0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GX_SetNumTevStages(1);

	mb.reset();
	for (int i = 0; i < 3; i++) {
		beginSimple(GX_TRIANGLES, 3);
		for (int k = 0; k < 3; k++) vtx(0, 0, 0, 1, 0, 0, 0, 0);
		GX_End();
	}
	CHECK(mb.texCreated == 1);                                  /* decoded once, cached by address */
	CHECK(mb.last.tex[0].sampler.wrapS == GX_CLAMP && mb.last.tex[0].sampler.wrapT == GX_REPEAT);
	CHECK(mb.last.tex[0].sampler.minLinear == 0 && mb.last.tex[0].sampler.magLinear == 1);

	DCFlushRange(a, 2048);                                        /* contents may have changed */
	beginSimple(GX_TRIANGLES, 3);
	for (int k = 0; k < 3; k++) vtx(0, 0, 0, 1, 0, 0, 0, 0);
	GX_End();
	CHECK(mb.texCreated == 2 && mb.texDestroyed == 1);

	__lwp_heap_free(&heap, a);                                    /* freed: entry dropped */
	CHECK(mb.texDestroyed == 2);
	CHECK(heap.used == 2048);
	__lwp_heap_free(&heap, b);
	CHECK(heap.used == 0);

	/* EFB copy: a render texture keyed by the destination address survives DCFlushRange */
	void *fb = __lwp_heap_allocate(&heap, 640 * 480 * 2);
	CHECK(fb == 0);                                               /* bigger than the 4 KiB budget */
	__lwp_heap_init(&heap, 0, 1 << 20, 32);
	fb = __lwp_heap_allocate(&heap, 64 * 64 * 2);
	GX_SetTexCopySrc(0, 0, 64, 64);
	GX_SetTexCopyDst(64, 64, GX_TF_RGB565, GX_FALSE);
	mb.reset();
	GX_CopyTex(fb, GX_FALSE);
	CHECK(mb.renderTex == 1 && mb.copies == 1);
	GX_InitTexObj(&obj, fb, 64, 64, GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);
	GX_LoadTexObj(&obj, GX_TEXMAP0);
	DCFlushRange(fb, 64 * 64 * 2);
	beginSimple(GX_TRIANGLES, 3);
	for (int k = 0; k < 3; k++) vtx(0, 0, 0, 1, 0, 0, 0, 0);
	GX_End();
	CHECK(mb.texCreated == 0 && mb.last.tex[0].tex == (TexHandle) (uintptr_t) 0x9001);
	GX_CopyTex(fb, GX_FALSE);                                     /* second copy reuses the texture */
	CHECK(mb.renderTex == 1 && mb.copies == 2);
	__lwp_heap_free(&heap, fb);
}

static void testClearAndPresent(MockBackend &mb)
{
	mb.reset();
	GX_SetColorUpdate(GX_ENABLE); GX_SetAlphaUpdate(GX_ENABLE);
	GX_SetZMode(GX_ENABLE, GX_ALWAYS, GX_TRUE);
	GX_SetCopyClear((GXColor) { 255, 0, 128, 255 }, GX_MAX_Z24);
	GX_SetDispCopySrc(0, 0, 640, 480);
	GX_CopyDisp(0, GX_TRUE);                                      /* clear only, no frame */
	CHECK(mb.clears == 1 && mb.presents == 0 && mb.lastMask == 15 && mb.lastClearDepth);
	CHECK_NEAR(mb.lastClear[2], 128.0 / 255.0, 1e-6);
	CHECK_NEAR(mb.lastDepth, 1.0, 1e-9);
	GX_SetColorUpdate(GX_DISABLE);
	GX_SetZMode(GX_ENABLE, GX_ALWAYS, GX_FALSE);
	GX_CopyDisp(0, GX_TRUE);
	CHECK(mb.clears == 2 && mb.lastMask == 8 && !mb.lastClearDepth);
	GX_SetColorUpdate(GX_ENABLE);

	static int syncToken = -1;
	GX_SetDrawSyncCallback([](u16 t) { syncToken = t; });
	GX_SetDispCopyGamma(GX_GM_2_2);
	char fake[4];
	GX_CopyDisp(fake, GX_FALSE);
	CHECK(mb.presents == 1 && mb.presentW == 1280 && mb.presentH == 960);
	GX_SetDrawSync(1);
	CHECK(syncToken == 1);
	CHECK_NEAR(presentGamma(), 2.2, 1e-6);
	GX_SetDrawSyncCallback(0);
}

static void testMaps()
{
	/* GX blend factors are positional: "SRCCLR" on the source side means the destination colour */
	CHECK(mapBlendFactor(GX_BL_SRCCLR, false) == G2_BLEND_DST_COLOR);
	CHECK(mapBlendFactor(GX_BL_SRCCLR, true) == G2_BLEND_SRC_COLOR);
	CHECK(mapBlendFactor(GX_BL_INVSRCCLR, false) == G2_BLEND_INV_DST_COLOR);
	CHECK(mapBlendFactor(GX_BL_INVSRCCLR, true) == G2_BLEND_INV_SRC_COLOR);
	CHECK(mapBlendFactor(GX_BL_SRCALPHA, false) == G2_BLEND_SRC_ALPHA && mapBlendFactor(GX_BL_INVSRCALPHA, true) == G2_BLEND_INV_SRC_ALPHA);
	CHECK(mapBlendFactor(GX_BL_DSTALPHA, false) == G2_BLEND_DST_ALPHA && mapBlendFactor(GX_BL_INVDSTALPHA, true) == G2_BLEND_INV_DST_ALPHA);
	CHECK(mapBlendFactor(GX_BL_ZERO, true) == G2_BLEND_ZERO && mapBlendFactor(GX_BL_ONE, false) == G2_BLEND_ONE);
	/* logic ops: truth table with S = 0xCC and D = 0xAA */
	for (int op = 0; op < 16; op++) {
		int rop = mapLogicOp(op);
		int S = 0xCC, D = 0xAA, expect = -1;
		switch (op) {
		case GX_LO_CLEAR: expect = 0; break;
		case GX_LO_AND: expect = S & D; break;
		case GX_LO_REVAND: expect = S & ~D; break;
		case GX_LO_COPY: expect = S; break;
		case GX_LO_INVAND: expect = ~S & D; break;
		case GX_LO_NOOP: expect = D; break;
		case GX_LO_XOR: expect = S ^ D; break;
		case GX_LO_OR: expect = S | D; break;
		case GX_LO_NOR: expect = ~(S | D); break;
		case GX_LO_EQUIV: expect = ~(S ^ D); break;
		case GX_LO_INV: expect = ~D; break;
		case GX_LO_REVOR: expect = S | ~D; break;
		case GX_LO_INVCOPY: expect = ~S; break;
		case GX_LO_INVOR: expect = ~S | D; break;
		case GX_LO_NAND: expect = ~(S & D); break;
		case GX_LO_SET: expect = 0xFF; break;
		}
		CHECK(rop == (expect & 0xFF));
	}
	CHECK(mapWrap(GX_REPEAT) == 0 && mapWrap(GX_MIRROR) == 1 && mapWrap(GX_CLAMP) == 2);
	CHECK(mapChannelMask(true, false) == 7 && mapChannelMask(false, true) == 8 && mapChannelMask(true, true) == 15);
}

static void testGuMath()
{
	Mtx44 m, inv;
	guOrtho(m, 0, 480, 0, 640, 0, 1);
	CHECK_NEAR(m[0][0], 2.0 / 640, 1e-9); CHECK_NEAR(m[0][3], -1.0, 1e-9);
	CHECK_NEAR(m[1][1], -2.0 / 480, 1e-9); CHECK_NEAR(m[1][3], 1.0, 1e-9);
	CHECK_NEAR(m[2][2], -1.0, 1e-9); CHECK_NEAR(m[2][3], -1.0, 1e-9);
	CHECK(guMtx44Inverse(m, inv) == 1);
	for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) {
		float s = 0; for (int k = 0; k < 4; k++) s += m[i][k] * inv[k][j];
		CHECK_NEAR(s, i == j ? 1.0 : 0.0, 1e-5);
	}
	Mtx44 z; memset(z, 0, sizeof(z));
	CHECK(guMtx44Inverse(z, inv) == 0);
	guVector v = { 3, 0, 4 }, u;
	guVecNormalize(&v, &u);
	CHECK_NEAR(u.x, 0.6, 1e-6); CHECK_NEAR(u.z, 0.8, 1e-6);
	guVecNormalize(&v, &v);
	CHECK_NEAR(v.z, 0.8, 1e-6);
	CHECK_NEAR(guVecDotProduct(&u, &u), 1.0, 1e-6);
}

/* Dumps random states with the reference result for tools/check-shader-sim.sh (python simulation of the generated shader). */
static void dumpVectors(const char *path)
{
	FILE *f = fopen(path, "w");
	if (!f) { printf("cannot write %s\n", path); gFail++; return; }
	srand(777);
	for (int iter = 0; iter < 300; iter++) {
		TevState t; tevReset(t);
		t.numStages = (uint8_t) (1 + iter % 7);
		for (int r = 1; r < 4; r++) for (int k = 0; k < 4; k++) t.reg[r][k] = frand();
		for (int r = 0; r < 4; r++) for (int k = 0; k < 4; k++) t.kcolor[r][k] = frand();
		for (int s = 0; s < t.numStages; s++) {
			TevStage &st = t.stage[s];
			st.texmap = (uint8_t) (rand() % 3 == 0 ? GX_TEXMAP_NULL : rand() % 3);
			st.color = (uint8_t) (rand() % 4 == 0 ? GX_COLOR_NULL : GX_COLOR0A0);
			for (int i = 0; i < 4; i++) { st.cIn[i] = (uint8_t) (rand() % 16); st.aIn[i] = (uint8_t) (rand() % 8); }
			st.cOp = (uint8_t) (rand() % 2); st.aOp = (uint8_t) (rand() % 2);
			st.cBias = (uint8_t) (rand() % 3); st.aBias = (uint8_t) (rand() % 3);
			st.cScale = (uint8_t) (rand() % 4); st.aScale = (uint8_t) (rand() % 4);
			st.cClamp = (uint8_t) (rand() % 2); st.aClamp = (uint8_t) (rand() % 2);
			st.cOut = (uint8_t) (rand() % 4); st.aOut = (uint8_t) (rand() % 4);
			st.kcSel = (uint8_t) (rand() % 2 ? rand() % 8 : 0x0C + rand() % 20);
			st.kaSel = (uint8_t) (rand() % 2 ? rand() % 8 : 0x10 + rand() % 16);
		}
		Rgba ras = rgba(frand(), frand(), frand(), frand());
		Rgba tex[2] = { rgba(frand(), frand(), frand(), frand()), rgba(frand(), frand(), frand(), frand()) };
		PsFixedParams fp; memset(&fp, 0, sizeof(fp));
		float ps[kPsUniformVec4 * 4];
		int n = tevPack(t, fp, ps);
		Rgba e = tevEvaluate(t, ras, tex);
		fprintf(f, "case %d\n", n);
		for (int i = 0; i < kPsUniformVec4 * 4; i++) fprintf(f, "%.9g ", ps[i]);
		fprintf(f, "\n%.9g %.9g %.9g %.9g\n%.9g %.9g %.9g %.9g\n%.9g %.9g %.9g %.9g\n%.9g %.9g %.9g %.9g\n",
			ras.c[0], ras.c[1], ras.c[2], ras.c[3], tex[0].c[0], tex[0].c[1], tex[0].c[2], tex[0].c[3],
			tex[1].c[0], tex[1].c[1], tex[1].c[2], tex[1].c[3], e.c[0], e.c[1], e.c[2], e.c[3]);
	}
	fclose(f);
}

int main(int argc, char **argv)
{
	if (argc > 1) {
		dumpVectors(argv[1]);
		return gFail ? 1 : 0;
	}
	testTevHandVectors();
	testTevPassThroughDrop();
	testTevPackedMatchesReference();
	testKonstSelection();
	testSwapCompMap();
	testTexDecode();
	testGuMath();
	testMaps();

	MockBackend mb;
	coreSetBackend(&mb, 2);
	testVertexAssembly(mb);
	testMatricesAndDepth(mb);
	testStateMapping(mb);
	testTextureCache(mb);
	testClearAndPresent(mb);
	coreSetBackend(0, 1);

	printf("%d checks, %d failures\n", gChecks, gFail);
	return gFail ? 1 : 0;
}
