/*
 * Link test for the GX2 renderer: init, one frame with a textured triangle through two TEV
 * stages, shutdown. This only proves that everything compiles and links for Wii U; it has
 * never been run (no hardware or emulator available when it was written).
 */
#include <string.h>

#include <gx2/state.h>
#include <whb/gfx.h>
#include <whb/log.h>
#include <whb/log_console.h>
#include <whb/proc.h>

#include "../../../wiiu/source/gx2gx/Gx2Gx.h"
#include "../../../wiiu/source/gx2gx/compat/gccore.h"

static int sPresents;

/* the app's video driver provides this in the real build */
extern "C" void Wiiu_PresentEmuFrame(GX2Texture *colour, int width, int height)
{
	(void) colour;
	WHBLogPrintf("present %dx%d", width, height);
	sPresents++;
}

/* an 8x8 RGB5A3 checkerboard in the GX tiled layout glN64_GX writes (two 4x4 blocks across, two down) */
static u16 sTexture[64] __attribute__((aligned(32)));

static void buildTexture()
{
	for (int by = 0; by < 2; by++)
		for (int bx = 0; bx < 2; bx++)
			for (int ty = 0; ty < 4; ty++)
				for (int tx = 0; tx < 4; tx++) {
					bool on = (((bx * 4 + tx) / 2) + ((by * 4 + ty) / 2)) & 1;
					sTexture[(by * 2 + bx) * 16 + ty * 4 + tx] = on ? 0xFFFF : 0x8A52;   /* white / grey, opaque */
				}
}

static void drawFrame()
{
	Mtx44 proj;
	Mtx pos;
	guOrtho(proj, 0, 480, 0, 640, 0, 1);
	guMtxIdentity(pos);
	GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);
	GX_LoadPosMtxImm(pos, GX_PNMTX0);
	GX_SetViewport(0, 0, 640, 480, 0.0f, 1.0f);
	GX_SetScissor(0, 0, 640, 480);
	GX_SetCullMode(GX_CULL_NONE);
	GX_SetZMode(GX_DISABLE, GX_ALWAYS, GX_FALSE);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
	GX_SetFog(GX_FOG_NONE, 0, 1, 0, 1, (GXColor){ 0, 0, 0, 255 });

	GX_SetCopyClear((GXColor){ 16, 32, 64, 255 }, GX_MAX_Z24);
	GX_SetColorUpdate(GX_ENABLE);
	GX_SetAlphaUpdate(GX_ENABLE);
	GX_SetZMode(GX_ENABLE, GX_ALWAYS, GX_TRUE);
	GX_CopyDisp(NULL, GX_TRUE);                         /* clear only */
	GX_SetZMode(GX_DISABLE, GX_ALWAYS, GX_FALSE);

	GXTexObj tex;
	GX_InitTexObj(&tex, sTexture, 8, 8, GX_TF_RGB5A3, GX_REPEAT, GX_REPEAT, GX_FALSE);
	GX_InitTexObjLOD(&tex, GX_NEAR, GX_NEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
	GX_LoadTexObj(&tex, GX_TEXMAP0);

	/* two stages: stage 0 = texture * vertex colour, stage 1 = lerp towards register 0 by 1/4 of the previous alpha */
	GX_SetNumChans(1);
	GX_SetNumTexGens(1);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GX_SetNumTevStages(2);
	GX_SetTevOrder(0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	GX_SetTevOp(0, GX_MODULATE);
	GX_SetTevColor(GX_TEVREG0, (GXColor){ 255, 128, 0, 255 });
	GX_SetTevOrder(1, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
	GX_SetTevColorIn(1, GX_CC_CPREV, GX_CC_C0, GX_CC_HALF, GX_CC_ZERO);
	GX_SetTevColorOp(1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	GX_SetTevAlphaIn(1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
	GX_SetTevAlphaOp(1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);

	GX_Begin(GX_TRIANGLES, GX_VTXFMT0, 3);
	GX_Position3f32(120.0f, 400.0f, 0.0f); GX_Color4u8(255, 255, 255, 255); GX_TexCoord2f32(0.0f, 1.0f);
	GX_Position3f32(520.0f, 400.0f, 0.0f); GX_Color4u8(255, 200, 200, 255); GX_TexCoord2f32(2.0f, 1.0f);
	GX_Position3f32(320.0f, 80.0f, 0.0f);  GX_Color4u8(200, 255, 200, 255); GX_TexCoord2f32(1.0f, 0.0f);
	GX_End();

	GX_CopyDisp(sTexture, GX_FALSE);                    /* frame done: any non-NULL pointer means "present" */
	GX_DrawDone();
}

int main(int, char **)
{
	WHBProcInit();
	WHBLogConsoleInit();
	WHBGfxInit();

	buildTexture();
	if (gx2gx::init(1)) {
		drawFrame();
		gx2gx::shutdown();
		WHBLogPrintf("gx2gx test done, presents=%d", sPresents);
	} else {
		WHBLogPrintf("gx2gx::init failed");
	}
	WHBLogConsoleDraw();

	while (WHBProcIsRunning()) {
	}
	WHBGfxShutdown();
	WHBLogConsoleFree();
	WHBProcShutdown();
	return 0;
}
