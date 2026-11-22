/*
 * Gx2Tev.cpp: see Gx2Tev.h. Pure logic, no GX2 dependency.
 */
#include "Gx2Tev.h"
#include "compat/ogc/gx.h"
#include <string.h>

namespace gx2gx {

static const float kClampLoOff = -1024.0f / 255.0f; /* TEV 11-bit signed range */
static const float kClampHiOff = 1023.0f / 255.0f;

void tevReset(TevState &t)
{
	memset(&t, 0, sizeof(t));
	t.numStages = 1;
	t.numChans = 1;
	t.numTexGens = 0;
	for (int i = 0; i < kMaxTevStages; i++) {
		TevStage &s = t.stage[i];
		s.texcoord = GX_TEXCOORD_NULL;
		s.texmap = GX_TEXMAP_NULL;
		s.color = GX_COLOR_NULL;
		tevSetOp(t, i, GX_PASSCLR);
	}
	t.stage[0].texcoord = GX_TEXCOORD0;
	t.stage[0].texmap = GX_TEXMAP0;
	t.stage[0].color = GX_COLOR0A0;
	tevSetOp(t, 0, GX_REPLACE);
	for (int i = 0; i < 4; i++) {
		t.swap[i][0] = GX_CH_RED;
		t.swap[i][1] = GX_CH_GREEN;
		t.swap[i][2] = GX_CH_BLUE;
		t.swap[i][3] = GX_CH_ALPHA;
	}
	/* libogc's default swap tables 1..3 replicate one channel */
	for (int c = 0; c < 3; c++)
		t.swap[1 + c][0] = t.swap[1 + c][1] = t.swap[1 + c][2] = (uint8_t) c;
}

void tevSetOp(TevState &t, int stage, int mode)
{
	if (stage < 0 || stage >= kMaxTevStages)
		return;
	TevStage &s = t.stage[stage];
	uint8_t defc = GX_CC_RASC, defa = GX_CA_RASA;
	if (stage != GX_TEVSTAGE0) {
		defc = GX_CC_CPREV;
		defa = GX_CA_APREV;
	}
	uint8_t c[4], a[4];
	switch (mode) {
	case GX_MODULATE:
		c[0] = GX_CC_ZERO; c[1] = GX_CC_TEXC; c[2] = defc; c[3] = GX_CC_ZERO;
		a[0] = GX_CA_ZERO; a[1] = GX_CA_TEXA; a[2] = defa; a[3] = GX_CA_ZERO;
		break;
	case GX_DECAL:
		c[0] = defc; c[1] = GX_CC_TEXC; c[2] = GX_CC_TEXA; c[3] = GX_CC_ZERO;
		a[0] = GX_CA_ZERO; a[1] = GX_CA_ZERO; a[2] = GX_CA_ZERO; a[3] = defa;
		break;
	case GX_BLEND:
		c[0] = defc; c[1] = GX_CC_ONE; c[2] = GX_CC_TEXC; c[3] = GX_CC_ZERO;
		a[0] = GX_CA_ZERO; a[1] = GX_CA_TEXA; a[2] = defa; a[3] = GX_CA_ZERO;
		break;
	case GX_REPLACE:
		c[0] = GX_CC_ZERO; c[1] = GX_CC_ZERO; c[2] = GX_CC_ZERO; c[3] = GX_CC_TEXC;
		a[0] = GX_CA_ZERO; a[1] = GX_CA_ZERO; a[2] = GX_CA_ZERO; a[3] = GX_CA_TEXA;
		break;
	case GX_PASSCLR:
	default:
		c[0] = GX_CC_ZERO; c[1] = GX_CC_ZERO; c[2] = GX_CC_ZERO; c[3] = defc;
		a[0] = GX_CA_ZERO; a[1] = GX_CA_ZERO; a[2] = GX_CA_ZERO; a[3] = defa;
		break;
	}
	for (int i = 0; i < 4; i++) {
		s.cIn[i] = c[i];
		s.aIn[i] = a[i];
	}
	s.cOp = s.aOp = GX_TEV_ADD;
	s.cBias = s.aBias = GX_TB_ZERO;
	s.cScale = s.aScale = GX_CS_SCALE_1;
	s.cClamp = s.aClamp = GX_TRUE;
	s.cOut = s.aOut = GX_TEVPREV;
}

static bool isPassThrough(const TevStage &s)
{
	return s.cIn[0] == GX_CC_ZERO && s.cIn[1] == GX_CC_ZERO && s.cIn[2] == GX_CC_ZERO && s.cIn[3] == GX_CC_CPREV &&
	       s.aIn[0] == GX_CA_ZERO && s.aIn[1] == GX_CA_ZERO && s.aIn[2] == GX_CA_ZERO && s.aIn[3] == GX_CA_APREV &&
	       s.cOp == GX_TEV_ADD && s.aOp == GX_TEV_ADD && s.cBias == GX_TB_ZERO && s.aBias == GX_TB_ZERO &&
	       s.cScale == GX_CS_SCALE_1 && s.aScale == GX_CS_SCALE_1 && s.cOut == GX_TEVPREV && s.aOut == GX_TEVPREV;
}

int tevEffectiveStages(const TevState &t)
{
	int n = t.numStages;
	if (n < 1)
		n = 1;
	if (n > kMaxTevStages)
		n = kMaxTevStages;
	/* glN64 appends a PASSCLR stage for the Z texture: identity, so it costs nothing to drop. */
	while (n > 1 && isPassThrough(t.stage[n - 1]))
		n--;
	if (n > kShaderTevStages)
		n = kShaderTevStages;
	return n;
}

static float kFrac(int sel)
{
	/* GX_TEV_KCSEL_8_8 .. 1_8 */
	return (float) (8 - sel) / 8.0f;
}

void tevKonstColor(const TevState &t, int sel, float out[3])
{
	out[0] = out[1] = out[2] = 0.0f;
	if (sel <= GX_TEV_KCSEL_1_8) {
		out[0] = out[1] = out[2] = kFrac(sel);
	} else if (sel >= GX_TEV_KCSEL_K0 && sel <= GX_TEV_KCSEL_K3) {
		const float *k = t.kcolor[sel - GX_TEV_KCSEL_K0];
		out[0] = k[0]; out[1] = k[1]; out[2] = k[2];
	} else if (sel >= GX_TEV_KCSEL_K0_R && sel <= GX_TEV_KCSEL_K3_A) {
		int chan = (sel - GX_TEV_KCSEL_K0_R) >> 2;      /* 0 r, 1 g, 2 b, 3 a */
		int reg = (sel - GX_TEV_KCSEL_K0_R) & 3;
		out[0] = out[1] = out[2] = t.kcolor[reg][chan];
	}
}

float tevKonstAlpha(const TevState &t, int sel)
{
	if (sel <= GX_TEV_KASEL_1_8)
		return kFrac(sel);
	if (sel >= GX_TEV_KASEL_K0_R && sel <= GX_TEV_KASEL_K3_A) {
		int chan = (sel - GX_TEV_KASEL_K0_R) >> 2;
		int reg = (sel - GX_TEV_KASEL_K0_R) & 3;
		return t.kcolor[reg][chan];
	}
	return 0.0f;
}

static float scaleOf(int s)
{
	switch (s) {
	case GX_CS_SCALE_2: return 2.0f;
	case GX_CS_SCALE_4: return 4.0f;
	case GX_CS_DIVIDE_2: return 0.5f;
	default: return 1.0f;
	}
}

static float biasOf(int b)
{
	switch (b) {
	case GX_TB_ADDHALF: return 0.5f;
	case GX_TB_SUBHALF: return -0.5f;
	default: return 0.0f;
	}
}

/* candidate for a texmap, -1 when the shader has no such texture */
static int texCand(int texmap)
{
	if (texmap == GX_TEXMAP0) return CAND_TEX0;
	if (texmap == GX_TEXMAP1) return CAND_TEX1;
	return -1;
}

/* A resolved input: either "weight * candidate" in some form, or a constant. */
struct Term {
	int cand;       /* -1: constant / none */
	int form;       /* 0 rgb, 1 aaa (colour inputs) */
	float konst[3]; /* constant colour contribution */
};

static Term colourTerm(const TevState &t, const TevStage &s, int code)
{
	Term r;
	r.cand = -1; r.form = 0; r.konst[0] = r.konst[1] = r.konst[2] = 0.0f;
	switch (code) {
	case GX_CC_CPREV: r.cand = CAND_PREV; break;
	case GX_CC_APREV: r.cand = CAND_PREV; r.form = 1; break;
	case GX_CC_C0: r.cand = CAND_REG0; break;
	case GX_CC_A0: r.cand = CAND_REG0; r.form = 1; break;
	case GX_CC_C1: r.cand = CAND_REG1; break;
	case GX_CC_A1: r.cand = CAND_REG1; r.form = 1; break;
	case GX_CC_C2: r.cand = CAND_REG2; break;
	case GX_CC_A2: r.cand = CAND_REG2; r.form = 1; break;
	case GX_CC_TEXC: r.cand = texCand(s.texmap); break;
	case GX_CC_TEXA: r.cand = texCand(s.texmap); r.form = 1; break;
	case GX_CC_RASC: if (s.color == GX_COLOR0A0 || s.color == GX_COLOR0) r.cand = CAND_RAS; break;
	case GX_CC_RASA: if (s.color == GX_COLOR0A0 || s.color == GX_ALPHA0) r.cand = CAND_RAS; r.form = 1; break;
	case GX_CC_ONE: r.konst[0] = r.konst[1] = r.konst[2] = 1.0f; break;
	case GX_CC_HALF: r.konst[0] = r.konst[1] = r.konst[2] = 0.5f; break;
	case GX_CC_KONST: tevKonstColor(t, s.kcSel, r.konst); break;
	default: break; /* ZERO */
	}
	if (r.cand < 0 && (code == GX_CC_RASC || code == GX_CC_RASA))
		r.form = 0;
	return r;
}

/* alpha input: returns candidate (or -1) and constant */
static int alphaTerm(const TevState &t, const TevStage &s, int code, float *konst)
{
	*konst = 0.0f;
	switch (code) {
	case GX_CA_APREV: return CAND_PREV;
	case GX_CA_A0: return CAND_REG0;
	case GX_CA_A1: return CAND_REG1;
	case GX_CA_A2: return CAND_REG2;
	case GX_CA_TEXA: return texCand(s.texmap);
	case GX_CA_RASA: return (s.color == GX_COLOR0A0 || s.color == GX_ALPHA0) ? (int) CAND_RAS : -1;
	case GX_CA_KONST: *konst = tevKonstAlpha(t, s.kaSel); return -1;
	default: return -1; /* ZERO */
	}
}

int tevPack(const TevState &t, const PsFixedParams &fixed, float *ps)
{
	memset(ps, 0, sizeof(float) * 4 * kPsUniformVec4);
	for (int r = 0; r < 3; r++)
		for (int k = 0; k < 4; k++)
			ps[(0 + r) * 4 + k] = t.reg[1 + r][k];
	ps[3 * 4 + 0] = fixed.fogA;
	ps[3 * 4 + 1] = fixed.fogB;
	ps[3 * 4 + 2] = fixed.fogEnable;
	for (int k = 0; k < 3; k++)
		ps[4 * 4 + k] = fixed.fogColor[k];
	ps[5 * 4 + 0] = fixed.dstAlphaEnable;
	ps[5 * 4 + 1] = fixed.dstAlpha;

	int n = tevEffectiveStages(t);
	for (int si = 0; si < n; si++) {
		const TevStage &s = t.stage[si];
		float *blk = ps + (kPsStageBase + si * kPsStageSize) * 4;
		float cSign = (s.cOp == GX_TEV_SUB) ? -1.0f : 1.0f;
		float aSign = (s.aOp == GX_TEV_SUB) ? -1.0f : 1.0f;
		float cSc = scaleOf(s.cScale), aSc = scaleOf(s.aScale);
		float cf[4] = { cSign * cSc, cSign * cSc, 1.0f, cSc };
		float af[4] = { aSign * aSc, aSign * aSc, 1.0f, aSc };
		float *kst = blk + 28 * 4; /* constants for inputs a,b,c,d at vec4 28..31 */

		for (int i = 0; i < 4; i++) {
			Term ct = colourTerm(t, s, s.cIn[i]);
			if (ct.cand >= 0) {
				float *w = blk + (i * kPsCandidates + ct.cand) * 4;
				w[ct.form ? 1 : 0] += cf[i];
			}
			for (int k = 0; k < 3; k++)
				kst[i * 4 + k] += ct.konst[k] * cf[i];

			float ka;
			int ac = alphaTerm(t, s, s.aIn[i], &ka);
			if (ac >= 0)
				blk[(i * kPsCandidates + ac) * 4 + 2] += af[i];
			kst[i * 4 + 3] += ka * af[i];
		}
		/* bias goes through the constant of d, scaled like d */
		float cb = biasOf(s.cBias) * cSc, ab = biasOf(s.aBias) * aSc;
		for (int k = 0; k < 3; k++)
			kst[3 * 4 + k] += cb;
		kst[3 * 4 + 3] += ab;

		/* pad lanes */
		blk[PAD_CLAMP_LO_C * 4 + 3] = s.cClamp ? 0.0f : kClampLoOff;
		blk[PAD_CLAMP_HI_C * 4 + 3] = s.cClamp ? 1.0f : kClampHiOff;
		blk[PAD_CLAMP_LO_A * 4 + 3] = s.aClamp ? 0.0f : kClampLoOff;
		blk[PAD_CLAMP_HI_A * 4 + 3] = s.aClamp ? 1.0f : kClampHiOff;
		for (int r = 0; r < 4; r++) {
			blk[(PAD_OUT_C0 + r) * 4 + 3] = (s.cOut == r) ? 1.0f : 0.0f;
			blk[(PAD_OUT_A0 + r) * 4 + 3] = (s.aOut == r) ? 1.0f : 0.0f;
		}
	}
	return n;
}

uint32_t tevTexCompMap(const TevState &t, int texmap)
{
	uint8_t tab = GX_TEV_SWAP0;
	int n = t.numStages > kMaxTevStages ? kMaxTevStages : t.numStages;
	for (int i = 0; i < n; i++) {
		if (t.stage[i].texmap == texmap) {
			tab = t.stage[i].texSwap & 3;
			break;
		}
	}
	const uint8_t *sw = t.swap[tab];
	return ((uint32_t) sw[0] << 24) | ((uint32_t) sw[1] << 16) | ((uint32_t) sw[2] << 8) | sw[3];
}

/* ------------------------------------------------------------------------- */

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

Rgba tevEvaluate(const TevState &t, const Rgba &ras, const Rgba tex[2])
{
	Rgba reg[4];
	for (int r = 0; r < 4; r++)
		for (int k = 0; k < 4; k++)
			reg[r].c[k] = (r == 0) ? 0.0f : t.reg[r][k];

	int n = tevEffectiveStages(t);
	for (int si = 0; si < n; si++) {
		const TevStage &s = t.stage[si];
		const Rgba *tx = (s.texmap == GX_TEXMAP0) ? &tex[0] : (s.texmap == GX_TEXMAP1 ? &tex[1] : 0);
		const bool rasC = (s.color == GX_COLOR0A0 || s.color == GX_COLOR0);
		const bool rasA = (s.color == GX_COLOR0A0 || s.color == GX_ALPHA0);

		float ci[4][3], ai[4];
		for (int i = 0; i < 4; i++) {
			float v[3] = { 0, 0, 0 };
			switch (s.cIn[i]) {
			case GX_CC_CPREV: for (int k = 0; k < 3; k++) v[k] = reg[0].c[k]; break;
			case GX_CC_APREV: for (int k = 0; k < 3; k++) v[k] = reg[0].c[3]; break;
			case GX_CC_C0: for (int k = 0; k < 3; k++) v[k] = reg[1].c[k]; break;
			case GX_CC_A0: for (int k = 0; k < 3; k++) v[k] = reg[1].c[3]; break;
			case GX_CC_C1: for (int k = 0; k < 3; k++) v[k] = reg[2].c[k]; break;
			case GX_CC_A1: for (int k = 0; k < 3; k++) v[k] = reg[2].c[3]; break;
			case GX_CC_C2: for (int k = 0; k < 3; k++) v[k] = reg[3].c[k]; break;
			case GX_CC_A2: for (int k = 0; k < 3; k++) v[k] = reg[3].c[3]; break;
			case GX_CC_TEXC: if (tx) for (int k = 0; k < 3; k++) v[k] = tx->c[k]; break;
			case GX_CC_TEXA: if (tx) for (int k = 0; k < 3; k++) v[k] = tx->c[3]; break;
			case GX_CC_RASC: if (rasC) for (int k = 0; k < 3; k++) v[k] = ras.c[k]; break;
			case GX_CC_RASA: if (rasA) for (int k = 0; k < 3; k++) v[k] = ras.c[3]; break;
			case GX_CC_ONE: for (int k = 0; k < 3; k++) v[k] = 1.0f; break;
			case GX_CC_HALF: for (int k = 0; k < 3; k++) v[k] = 0.5f; break;
			case GX_CC_KONST: tevKonstColor(t, s.kcSel, v); break;
			default: break;
			}
			for (int k = 0; k < 3; k++) ci[i][k] = v[k];

			float a = 0.0f;
			switch (s.aIn[i]) {
			case GX_CA_APREV: a = reg[0].c[3]; break;
			case GX_CA_A0: a = reg[1].c[3]; break;
			case GX_CA_A1: a = reg[2].c[3]; break;
			case GX_CA_A2: a = reg[3].c[3]; break;
			case GX_CA_TEXA: if (tx) a = tx->c[3]; break;
			case GX_CA_RASA: if (rasA) a = ras.c[3]; break;
			case GX_CA_KONST: a = tevKonstAlpha(t, s.kaSel); break;
			default: break;
			}
			ai[i] = a;
		}

		float cSign = (s.cOp == GX_TEV_SUB) ? -1.0f : 1.0f;
		float aSign = (s.aOp == GX_TEV_SUB) ? -1.0f : 1.0f;
		float res[4];
		for (int k = 0; k < 3; k++) {
			float lerp = (1.0f - ci[2][k]) * ci[0][k] + ci[2][k] * ci[1][k];
			float v = (ci[3][k] + cSign * lerp + biasOf(s.cBias)) * scaleOf(s.cScale);
			res[k] = s.cClamp ? clampf(v, 0.0f, 1.0f) : clampf(v, kClampLoOff, kClampHiOff);
		}
		{
			float lerp = (1.0f - ai[2]) * ai[0] + ai[2] * ai[1];
			float v = (ai[3] + aSign * lerp + biasOf(s.aBias)) * scaleOf(s.aScale);
			res[3] = s.aClamp ? clampf(v, 0.0f, 1.0f) : clampf(v, kClampLoOff, kClampHiOff);
		}
		for (int k = 0; k < 3; k++)
			reg[s.cOut & 3].c[k] = res[k];
		reg[s.aOut & 3].c[3] = res[3];
	}
	return reg[0];
}

Rgba tevEvaluatePacked(const float *ps, int stages, const Rgba &ras, const Rgba tex[2])
{
	Rgba reg[4];
	for (int r = 0; r < 4; r++)
		for (int k = 0; k < 4; k++)
			reg[r].c[k] = (r == 0) ? 0.0f : ps[(r - 1) * 4 + k];
	for (int si = 0; si < stages; si++) {
		const float *blk = ps + (kPsStageBase + si * kPsStageSize) * 4;
		const Rgba *cand[kPsCandidates] = { &reg[0], &reg[1], &reg[2], &reg[3], &tex[0], &tex[1], &ras };
		float in[4][4];
		for (int i = 0; i < 4; i++) {
			for (int k = 0; k < 4; k++)
				in[i][k] = blk[(28 + i) * 4 + k];
			for (int j = 0; j < kPsCandidates; j++) {
				const float *w = blk + (i * kPsCandidates + j) * 4;
				for (int k = 0; k < 3; k++)
					in[i][k] += w[0] * cand[j]->c[k] + w[1] * cand[j]->c[3];
				in[i][3] += w[2] * cand[j]->c[3];
			}
		}
		float out[4];
		for (int k = 0; k < 4; k++) {
			float v = in[3][k] + (in[0][k] + in[2][k] * (in[1][k] - in[0][k]));
			float lo = blk[(k < 3 ? PAD_CLAMP_LO_C : PAD_CLAMP_LO_A) * 4 + 3];
			float hi = blk[(k < 3 ? PAD_CLAMP_HI_C : PAD_CLAMP_HI_A) * 4 + 3];
			out[k] = clampf(v, lo, hi);
		}
		Rgba nreg[4] = { reg[0], reg[1], reg[2], reg[3] };
		for (int r = 0; r < 4; r++) {
			float oc = blk[(PAD_OUT_C0 + r) * 4 + 3];
			float oa = blk[(PAD_OUT_A0 + r) * 4 + 3];
			for (int k = 0; k < 3; k++)
				nreg[r].c[k] = reg[r].c[k] + oc * (out[k] - reg[r].c[k]);
			nreg[r].c[3] = reg[r].c[3] + oa * (out[3] - reg[r].c[3]);
		}
		for (int r = 0; r < 4; r++)
			reg[r] = nreg[r];
	}
	return reg[0];
}

} /* namespace gx2gx */
