/*
 * Gx2Tev.h: TEV state, packing of that state into the ubershader's uniform
 * block, and a CPU reference implementation of the TEV equation.
 *
 * Pure C++ (no GX2): the same code runs in the host unit tests. The uniform
 * layout documented here is the contract with shaders/gen_shaders.py; if you
 * change one, change the other (tests/gx2gx cross-checks them).
 */
#ifndef GX2GX_TEV_H
#define GX2GX_TEV_H

#include <stdint.h>

namespace gx2gx {

static const int kMaxTevStages = 16;     /* GX limit (state is stored for all of them) */
static const int kShaderTevStages = 7;   /* stages the pixel shader evaluates */
static const int kPsUniformVec4 = 256;   /* pixel uniform register file, vec4 units */

/* One TEV stage exactly as set through GX_SetTev*() */
struct TevStage {
	uint8_t texcoord, texmap, color;      /* GX_SetTevOrder */
	uint8_t cIn[4], aIn[4];               /* GX_CC_* / GX_CA_* for a,b,c,d */
	uint8_t cOp, cBias, cScale, cClamp, cOut;
	uint8_t aOp, aBias, aScale, aClamp, aOut;
	uint8_t kcSel, kaSel;                 /* GX_TEV_KCSEL_* / GX_TEV_KASEL_* */
	uint8_t rasSwap, texSwap;             /* GX_TEV_SWAP0..3 */
};

struct TevState {
	uint8_t numStages;
	uint8_t numChans;
	uint8_t numTexGens;
	TevStage stage[kMaxTevStages];
	float reg[4][4];                      /* GX_TEVPREV(unused), REG0..REG2 as 0..1 floats */
	float kcolor[4][4];                   /* GX_KCOLOR0..3 as 0..1 floats */
	uint8_t swap[4][4];                   /* swap tables: output channel -> source channel */
};

/* Fog / alpha / dst-alpha parameters that live in the same uniform block */
struct PsFixedParams {
	float fogEnable, fogA, fogB;          /* f = clamp(zscreen*fogA + fogB) * fogEnable */
	float fogColor[4];
	float dstAlphaEnable, dstAlpha;       /* alpha out = lerp(a, dstAlpha, enable) */
};

/* Reset to the state GX_Init() leaves behind (1 stage, REPLACE, no channels). */
void tevReset(TevState &t);

/* libogc GX_SetTevOp() semantics (kept bit-identical, including the CPREV default for stage > 0). */
void tevSetOp(TevState &t, int stage, int mode);

/* Number of stages the shader must run for this state (trailing identity pass-through stages are dropped). */
int tevEffectiveStages(const TevState &t);

/* Resolve GX_TEV_KCSEL_* / KASEL_* to a value (rgb or single component, 0..1). */
void tevKonstColor(const TevState &t, int sel, float out[3]);
float tevKonstAlpha(const TevState &t, int sel);

/* ---- ubershader uniform layout (pixel shader constant registers, vec4 units) ----
 *   0..2  : reg0, reg1, reg2 initial values (rgba)
 *   3     : fog (fogA, fogB, fogEnable, 0)
 *   4     : fog colour (rgb, 0)
 *   5     : dst alpha (enable, value, 0, 0)
 *   8 + s*32 + [0..27] : weights, index i*7+j  (i = input a,b,c,d ; j = candidate)
 *                        = (w_rgb, w_aform, w_alpha, pad)
 *         candidates j: 0 prev, 1 reg0, 2 reg1, 3 reg2, 4 tex0, 5 tex1, 6 ras
 *         w_rgb   : weight of cand.rgb in the colour input's rgb
 *         w_aform : weight of cand.aaa in the colour input's rgb
 *         w_alpha : weight of cand.a in the alpha input's a
 *         pad lanes (.w): see PadSlot below
 *   8 + s*32 + [28..31] : constant added to input a,b,c,d (rgb = colour const, a = alpha const)
 * Folded on the CPU: op sign into the weights/constants of a and b, scale into a, b, d,
 * bias into the constant of d. The shader therefore only evaluates
 *      out = clamp( d + c*(b-a) + a )   per lane.
 */
static const int kPsStageBase = 8;
static const int kPsStageSize = 32;
static const int kPsCandidates = 7;

/* pad-lane slots inside a stage block (vec4 index i*7+j .w) */
enum PadSlot {
	PAD_CLAMP_LO_C = 0, PAD_CLAMP_HI_C = 1, PAD_CLAMP_LO_A = 2, PAD_CLAMP_HI_A = 3,
	PAD_OUT_C0 = 4,  /* 4..7  : colour output register one-hot (prev, reg0, reg1, reg2) */
	PAD_OUT_A0 = 8,  /* 8..11 : alpha output register one-hot */
	PAD_COUNT = 12
};

/* Candidate indices */
enum { CAND_PREV = 0, CAND_REG0, CAND_REG1, CAND_REG2, CAND_TEX0, CAND_TEX1, CAND_RAS };

/* Fill ps[256*4]; returns number of shader stages to run (1..kShaderTevStages). */
int tevPack(const TevState &t, const PsFixedParams &fixed, float *ps /* kPsUniformVec4*4 */);

/* Texture swizzle (GX2 compMap component selectors 0..3 = r,g,b,a) used for a texmap, from its first user stage. */
uint32_t tevTexCompMap(const TevState &t, int texmap);

/* ---- CPU reference of the TEV equation (libogc documentation), float version ---- */
struct Rgba { float c[4]; };

/* Evaluate the whole chain: ras = rasterised vertex colour, tex[2] = texel of texmap0/1. Returns PREV. */
Rgba tevEvaluate(const TevState &t, const Rgba &ras, const Rgba tex[2]);

/* Evaluate by reading the packed uniforms exactly like the shader does (used to prove tevPack faithful). */
Rgba tevEvaluatePacked(const float *ps, int stages, const Rgba &ras, const Rgba tex[2]);

} /* namespace gx2gx */

#endif
