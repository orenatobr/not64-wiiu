/*
 * ogc/gu.h (not64 GX2 shim): the guVec/guMtx helpers glN64_GX uses, with the
 * same names, types and conventions as libogc (row-major Mtx = 3x4, Mtx44 =
 * 4x4, GX NDC z range [-1, 0] for guOrtho/guFrustum style matrices).
 * Implemented in Gx2Math.cpp.
 */
#ifndef __OGC_GU_H__
#define __OGC_GU_H__

#include "../gctypes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _guvector {
	f32 x, y, z;
} guVector;

typedef f32 Mtx[3][4];
typedef f32 (*MtxP)[4];
typedef const f32 (*CMtxP)[4];
typedef f32 Mtx44[4][4];
typedef f32 (*Mtx44P)[4];
typedef const f32 (*CMtx44P)[4];

void guOrtho(Mtx44 mt, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f);
void guFrustum(Mtx44 mt, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f);

void guMtxIdentity(Mtx mt);
void guMtxConcat(const Mtx a, const Mtx b, Mtx ab);
void guMtx44Identity(Mtx44 mt);
/* returns 1 on success, 0 if the matrix is singular (inv is then untouched) */
u32 guMtx44Inverse(const Mtx44 src, Mtx44 inv);

/* src and unit may alias */
void guVecNormalize(const guVector *src, guVector *unit);
f32 guVecDotProduct(const guVector *a, const guVector *b);

#ifdef __cplusplus
}
#endif

#endif
