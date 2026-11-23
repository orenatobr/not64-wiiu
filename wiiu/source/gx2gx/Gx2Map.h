/*
 * Gx2Map.h: GX enum -> GX2 enum value mapping. Pure functions returning the numeric
 * value of the GX2 enumerator (checked against wut's enum.h with static_asserts in
 * Gx2Wiiu.cpp), so the tables are unit-testable on the host.
 */
#ifndef GX2GX_MAP_H
#define GX2GX_MAP_H

#include <stdint.h>
#include "compat/ogc/gx_defs.h"

namespace gx2gx {

/* GX2BlendMode values */
enum {
	G2_BLEND_ZERO = 0, G2_BLEND_ONE = 1, G2_BLEND_SRC_COLOR = 2, G2_BLEND_INV_SRC_COLOR = 3,
	G2_BLEND_SRC_ALPHA = 4, G2_BLEND_INV_SRC_ALPHA = 5, G2_BLEND_DST_ALPHA = 6, G2_BLEND_INV_DST_ALPHA = 7,
	G2_BLEND_DST_COLOR = 8, G2_BLEND_INV_DST_COLOR = 9
};

/*
 * GX blend factors are positional: for the source factor GX_BL_SRCCLR/GX_BL_DSTCLR (value 2)
 * means "destination colour", for the destination factor it means "source colour".
 */
static inline int mapBlendFactor(int gx, bool isDst)
{
	switch (gx) {
	case GX_BL_ZERO: return G2_BLEND_ZERO;
	case GX_BL_ONE: return G2_BLEND_ONE;
	case GX_BL_SRCCLR: return isDst ? G2_BLEND_SRC_COLOR : G2_BLEND_DST_COLOR;
	case GX_BL_INVSRCCLR: return isDst ? G2_BLEND_INV_SRC_COLOR : G2_BLEND_INV_DST_COLOR;
	case GX_BL_SRCALPHA: return G2_BLEND_SRC_ALPHA;
	case GX_BL_INVSRCALPHA: return G2_BLEND_INV_SRC_ALPHA;
	case GX_BL_DSTALPHA: return G2_BLEND_DST_ALPHA;
	case GX_BL_INVDSTALPHA: return G2_BLEND_INV_DST_ALPHA;
	default: return isDst ? G2_BLEND_ZERO : G2_BLEND_ONE;
	}
}

/* GX2LogicOp (ROP3) for GX_LO_*; the truth-table convention is S = 0xCC, D = 0xAA. */
static inline int mapLogicOp(int gx)
{
	static const uint8_t t[16] = {
		0x00, /* CLEAR  */ 0x88, /* AND    */ 0x44, /* REVAND: S & ~D */ 0xCC, /* COPY   */
		0x22, /* INVAND: ~S & D */ 0xAA, /* NOOP   */ 0x66, /* XOR    */ 0xEE, /* OR     */
		0x11, /* NOR    */ 0x99, /* EQUIV  */ 0x55, /* INV: ~D */ 0xDD, /* REVOR: S | ~D */
		0x33, /* INVCOPY: ~S */ 0xBB, /* INVOR: ~S | D */ 0x77, /* NAND   */ 0xFF  /* SET    */
	};
	return (gx >= 0 && gx < 16) ? t[gx] : 0xCC;
}

/* GX2TexClampMode: WRAP 0, MIRROR 1, CLAMP 2 */
static inline int mapWrap(int gx)
{
	switch (gx) {
	case GX_REPEAT: return 0;
	case GX_MIRROR: return 1;
	default: return 2;
	}
}

/* GX2ChannelMask: R=1 G=2 B=4 A=8 */
static inline int mapChannelMask(bool color, bool alpha) { return (color ? 7 : 0) | (alpha ? 8 : 0); }

} /* namespace gx2gx */

#endif
