/*
 * ogc/gx.h (not64 GX2 shim)
 *
 * The subset of libogc2's GX API that glN64_GX uses, with libogc-identical
 * names, enums (gx_defs.h) and signatures, implemented on top of GX2 in
 * Gx2Gx.cpp. Vertex data written between GX_Begin() and GX_End() is collected in
 * a CPU buffer and GX_End() issues the GX2 draw (see docs/GX2_RENDERER.md).
 */
#ifndef __OGC_GX_H__
#define __OGC_GX_H__

#include "../gctypes.h"
#include "gx_struct.h"
#include "gu.h"
#include "gx_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _gx_color {
	u8 r;
	u8 g;
	u8 b;
	u8 a;
} GXColor;

typedef struct _gx_colors10 {
	s16 r;
	s16 g;
	s16 b;
	s16 a;
} GXColorS10;

/* Same size as libogc's GXTexObj (8 words); the layout is private to the shim. */
typedef struct _gx_texobj {
	u32 val[8];
} GXTexObj;

typedef void (*GXDrawSyncCallback)(u16 token);

/* vertex description / format */
void GX_ClearVtxDesc(void);
void GX_SetVtxDesc(u8 attr, u8 type);
void GX_SetVtxAttrFmt(u8 vtxfmt, u32 vtxattr, u32 comptype, u32 compsize, u32 frac);
void GX_Begin(u8 primitve, u8 vtxfmt, u16 vtxcnt);
void GX_End(void);
void GX_Position3f32(f32 x, f32 y, f32 z);
void GX_Position2f32(f32 x, f32 y);
void GX_Color4u8(u8 r, u8 g, u8 b, u8 a);
void GX_TexCoord2f32(f32 s, f32 t);

/* matrices */
void GX_LoadProjectionMtx(const Mtx44 mt, u8 type);
void GX_LoadPosMtxImm(const Mtx mt, u32 pnidx);
void GX_LoadTexMtxImm(const Mtx mt, u32 texidx, u8 type);
void GX_SetCurrentMtx(u32 mtx);

/* raster / pixel state */
void GX_SetViewport(f32 xOrig, f32 yOrig, f32 wd, f32 ht, f32 nearZ, f32 farZ);
void GX_SetScissor(u32 xOrigin, u32 yOrigin, u32 wd, u32 ht);
void GX_SetCullMode(u8 mode);
void GX_SetClipMode(u8 mode);
void GX_SetCoPlanar(u8 enable);
void GX_SetLineWidth(u8 width, u8 fmt);
void GX_SetZMode(u8 enable, u8 func, u8 update_enable);
void GX_SetZCompLoc(u8 before_tex);
void GX_SetZTexture(u8 op, u8 fmt, u32 bias);
void GX_SetBlendMode(u8 type, u8 src_fact, u8 dst_fact, u8 op);
void GX_SetColorUpdate(u8 enable);
void GX_SetAlphaUpdate(u8 enable);
void GX_SetDstAlpha(u8 enable, u8 a);
void GX_SetAlphaCompare(u8 comp0, u8 ref0, u8 aop, u8 comp1, u8 ref1);
void GX_SetFog(u8 type, f32 startz, f32 endz, f32 nearz, f32 farz, GXColor col);

/* TEV */
void GX_SetNumChans(u8 num);
void GX_SetNumTexGens(u32 nr);
void GX_SetNumTevStages(u8 num);
void GX_SetTexCoordGen(u16 texcoord, u32 tgen_typ, u32 tgen_src, u32 mtxsrc);
void GX_SetTevOrder(u8 tevstage, u8 texcoord, u32 texmap, u8 color);
void GX_SetTevOp(u8 tevstage, u8 mode);
void GX_SetTevColorIn(u8 tevstage, u8 a, u8 b, u8 c, u8 d);
void GX_SetTevAlphaIn(u8 tevstage, u8 a, u8 b, u8 c, u8 d);
void GX_SetTevColorOp(u8 tevstage, u8 tevop, u8 tevbias, u8 tevscale, u8 clamp, u8 tevregid);
void GX_SetTevAlphaOp(u8 tevstage, u8 tevop, u8 tevbias, u8 tevscale, u8 clamp, u8 tevregid);
void GX_SetTevColor(u8 tev_regid, GXColor color);
void GX_SetTevKColor(u8 sel, GXColor col);
void GX_SetTevKColorSel(u8 tevstage, u8 sel);
void GX_SetTevKAlphaSel(u8 tevstage, u8 sel);
void GX_SetTevSwapMode(u8 tevstage, u8 ras_sel, u8 tex_sel);
void GX_SetTevSwapModeTable(u8 swapid, u8 r, u8 g, u8 b, u8 a);

/* textures */
void GX_InitTexObj(GXTexObj *obj, void *img_ptr, u16 wd, u16 ht, u8 fmt, u8 wrap_s, u8 wrap_t, u8 mipmap);
void GX_InitTexObjLOD(GXTexObj *obj, u8 minfilt, u8 magfilt, f32 minlod, f32 maxlod, f32 lodbias, u8 biasclamp, u8 edgelod, u8 maxaniso);
void GX_LoadTexObj(const GXTexObj *obj, u8 mapid);
void GX_InvalidateTexAll(void);

/* EFB copies */
void GX_SetTexCopySrc(u16 left, u16 top, u16 wd, u16 ht);
void GX_SetTexCopyDst(u16 wd, u16 ht, u32 fmt, u8 mode);
void GX_CopyTex(void *dest, u8 clear);
void GX_SetDispCopySrc(u16 left, u16 top, u16 wd, u16 ht);
void GX_SetDispCopyDst(u16 wd, u16 ht);
void GX_SetDispCopyFrame2Field(u8 mode);
void GX_SetDispCopyGamma(u8 gamma);
void GX_SetCopyFilter(u8 aa, const u8 sample_pattern[12][2], u8 vf, const u8 vfilter[7]);
void GX_SetCopyClear(GXColor color, u32 zvalue);
void GX_CopyDisp(void *dest, u8 clear);

/* synchronisation */
void GX_PixModeSync(void);
void GX_DrawDone(void);
void GX_Flush(void);
void GX_SetDrawSync(u16 token);
GXDrawSyncCallback GX_SetDrawSyncCallback(GXDrawSyncCallback cb);
void GX_RestoreWriteGatherPipe(void);
volatile void *GX_RedirectWriteGatherPipe(void *ptr);

#ifdef __cplusplus
}
#endif

#endif
