/**
 * glN64_GX - GXcast.h
 *
 * Integer <-> float conversions that the GX build did with Gekko paired-single quantized loads and stores
 * (psq_l / psq_st with GQR2 for 8 bit and GQR3 for 16 bit). Those depend on the GQR scale that the Wii GX
 * initialisation sets. A Wii U title never runs that initialisation, the GQRs stay at zero and the
 * conversions did not scale: a G_TEXTURE scale of 0x8000 became 32768.0 instead of 0.5 and colours stayed in
 * 0..255 instead of 0..1. That blew up every texture coordinate (flat colour blocks and noisy surfaces) and
 * saturated lighting. Plain C does the same job on every CPU: 8 bit values scale by 1/256 and 16 bit values
 * by 1/65536, and the way back truncates with saturation like psq_st.
 *
 * Kept free of other includes so a host test can build it.
 **/

#ifndef GXCAST_H
#define GXCAST_H

static inline float GXcastu8f32( unsigned char val )
{
	return (float)val * (1.0f / 256.0f);
}

static inline float GXcastu16f32( unsigned short val )
{
	return (float)val * (1.0f / 65536.0f);
}

static inline unsigned char GXcastf32u8( float val )
{
	val *= 256.0f;
	if (!(val > 0.0f)) return 0;			/* also catches NaN */
	if (val >= 255.0f) return 255;
	return (unsigned char)val;
}

static inline unsigned short GXcastf32u16( float val )
{
	val *= 65536.0f;
	if (!(val > 0.0f)) return 0;
	if (val >= 65535.0f) return 65535;
	return (unsigned short)val;
}

#endif
