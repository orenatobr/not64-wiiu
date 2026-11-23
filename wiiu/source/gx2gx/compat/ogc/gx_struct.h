/*
 * ogc/gx_struct.h (not64 GX2 shim): GXRModeObj, field-for-field identical to libogc2.
 */
#ifndef __OGC_GX_STRUCT_H__
#define __OGC_GX_STRUCT_H__

#include "../gctypes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _gx_rmodeobj {
	u32 viTVMode;
	u16 fbWidth;
	u16 efbHeight;
	u16 xfbHeight;
	u16 viXOrigin;
	u16 viYOrigin;
	u16 viWidth;
	u16 viHeight;
	u32 xfbMode;
	u8  field_rendering;
	u8  aa;
	u8  sample_pattern[12][2];
	u8  vfilter[7];
	u8  copy_interlaced;
} GXRModeObj;

#ifdef __cplusplus
}
#endif

#endif
