/*
 * Gx2Math.cpp: the guMtx/guVec/guOrtho helpers (libogc conventions).
 */
#include "compat/ogc/gu.h"
#include <math.h>
#include <string.h>

extern "C" {

void guOrtho(Mtx44 p, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f)
{
	f32 tmp = 1.0f / (r - l);
	p[0][0] = 2.0f * tmp; p[0][1] = 0.0f; p[0][2] = 0.0f; p[0][3] = -(r + l) * tmp;
	tmp = 1.0f / (t - b);
	p[1][0] = 0.0f; p[1][1] = 2.0f * tmp; p[1][2] = 0.0f; p[1][3] = -(t + b) * tmp;
	tmp = 1.0f / (f - n);
	p[2][0] = 0.0f; p[2][1] = 0.0f; p[2][2] = -tmp; p[2][3] = -f * tmp;
	p[3][0] = 0.0f; p[3][1] = 0.0f; p[3][2] = 0.0f; p[3][3] = 1.0f;
}

void guFrustum(Mtx44 p, f32 t, f32 b, f32 l, f32 r, f32 n, f32 f)
{
	f32 tmp = 1.0f / (r - l);
	p[0][0] = 2.0f * n * tmp; p[0][1] = 0.0f; p[0][2] = (r + l) * tmp; p[0][3] = 0.0f;
	tmp = 1.0f / (t - b);
	p[1][0] = 0.0f; p[1][1] = 2.0f * n * tmp; p[1][2] = (t + b) * tmp; p[1][3] = 0.0f;
	p[2][0] = 0.0f; p[2][1] = 0.0f;
	tmp = 1.0f / (f - n);
	p[2][2] = -n * tmp; p[2][3] = -(f * n) * tmp;
	p[3][0] = 0.0f; p[3][1] = 0.0f; p[3][2] = -1.0f; p[3][3] = 0.0f;
}

void guMtxIdentity(Mtx m)
{
	memset(m, 0, sizeof(Mtx));
	m[0][0] = m[1][1] = m[2][2] = 1.0f;
}

void guMtx44Identity(Mtx44 m)
{
	memset(m, 0, sizeof(Mtx44));
	m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0f;
}

void guMtxConcat(const Mtx a, const Mtx b, Mtx ab)
{
	Mtx t;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 4; j++) {
			f32 v = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
			if (j == 3)
				v += a[i][3];
			t[i][j] = v;
		}
	memcpy(ab, t, sizeof(Mtx));
}

u32 guMtx44Inverse(const Mtx44 src, Mtx44 inv)
{
	/* cofactor expansion on a double copy, rows of src are m[row][col] */
	double m[16], r[16];
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			m[i * 4 + j] = src[i][j];

	r[0]  =  m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
	r[4]  = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
	r[8]  =  m[4] * m[9]  * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
	r[12] = -m[4] * m[9]  * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
	r[1]  = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
	r[5]  =  m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
	r[9]  = -m[0] * m[9]  * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
	r[13] =  m[0] * m[9]  * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
	r[2]  =  m[1] * m[6]  * m[15] - m[1] * m[7]  * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7]  - m[13] * m[3] * m[6];
	r[6]  = -m[0] * m[6]  * m[15] + m[0] * m[7]  * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7]  + m[12] * m[3] * m[6];
	r[10] =  m[0] * m[5]  * m[15] - m[0] * m[7]  * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7]  - m[12] * m[3] * m[5];
	r[14] = -m[0] * m[5]  * m[14] + m[0] * m[6]  * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6]  + m[12] * m[2] * m[5];
	r[3]  = -m[1] * m[6]  * m[11] + m[1] * m[7]  * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9]  * m[2] * m[7]  + m[9]  * m[3] * m[6];
	r[7]  =  m[0] * m[6]  * m[11] - m[0] * m[7]  * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8]  * m[2] * m[7]  - m[8]  * m[3] * m[6];
	r[11] = -m[0] * m[5]  * m[11] + m[0] * m[7]  * m[9]  + m[4] * m[1] * m[11] - m[4] * m[3] * m[9]  - m[8]  * m[1] * m[7]  + m[8]  * m[3] * m[5];
	r[15] =  m[0] * m[5]  * m[10] - m[0] * m[6]  * m[9]  - m[4] * m[1] * m[10] + m[4] * m[2] * m[9]  + m[8]  * m[1] * m[6]  - m[8]  * m[2] * m[5];

	double det = m[0] * r[0] + m[1] * r[4] + m[2] * r[8] + m[3] * r[12];
	if (det == 0.0)
		return 0;
	det = 1.0 / det;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			inv[i][j] = (f32) (r[i * 4 + j] * det);
	return 1;
}

void guVecNormalize(const guVector *src, guVector *unit)
{
	f32 l = sqrtf(src->x * src->x + src->y * src->y + src->z * src->z);
	if (l == 0.0f) {
		*unit = *src;
		return;
	}
	l = 1.0f / l;
	unit->x = src->x * l;
	unit->y = src->y * l;
	unit->z = src->z * l;
}

f32 guVecDotProduct(const guVector *a, const guVector *b)
{
	return a->x * b->x + a->y * b->y + a->z * b->z;
}

} /* extern "C" */
