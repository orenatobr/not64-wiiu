/* Host test for the glN64 integer/float casts (see glN64_GX/GXcast.h). */
#include <stdio.h>
#include <math.h>
#include "../glN64_GX/GXcast.h"

static int failures;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); failures++; } } while (0)

int main(void)
{
	/* The Banjo-Tooie bug: a G_TEXTURE scale of 0x8000 must be 0.5, not 32768. */
	CHECK(fabsf(GXcastu16f32(0x8000) - 0.5f) < 1e-6f);
	CHECK(GXcastu16f32(0) == 0.0f);
	CHECK(GXcastu16f32(0xFFFF) < 1.0f && GXcastu16f32(0xFFFF) > 0.9999f);

	/* 8 bit colours land in 0..1. */
	CHECK(GXcastu8f32(0) == 0.0f);
	CHECK(fabsf(GXcastu8f32(128) - 0.5f) < 1e-6f);
	CHECK(GXcastu8f32(255) < 1.0f && GXcastu8f32(255) > 0.99f);

	/* Round trips are exact for every value. */
	for (int v = 0; v < 256; v++) CHECK(GXcastf32u8(GXcastu8f32((unsigned char)v)) == v);
	for (int v = 0; v < 65536; v += 17) CHECK(GXcastf32u16(GXcastu16f32((unsigned short)v)) == v);

	/* Saturation and bad input. */
	CHECK(GXcastf32u8(1.0f) == 255);
	CHECK(GXcastf32u8(2.5f) == 255);
	CHECK(GXcastf32u8(-0.5f) == 0);
	CHECK(GXcastf32u8(NAN) == 0);
	CHECK(GXcastf32u16(1.0f) == 65535);
	CHECK(GXcastf32u16(-1.0f) == 0);
	CHECK(GXcastf32u16(NAN) == 0);

	if (failures) { printf("%d failure(s)\n", failures); return 1; }
	printf("test_gxcast: ok\n");
	return 0;
}
