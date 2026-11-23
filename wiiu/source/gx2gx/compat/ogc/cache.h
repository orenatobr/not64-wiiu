/*
 * ogc/cache.h (not64 GX2 shim): CPU cache helpers.
 *
 * On Wii U the GPU does not share a coherent cache with the game thread the way
 * the GameCube/Wii GX FIFO did, but texture data is converted/uploaded by the
 * shim itself at GX_LoadTexObj time, so these only need to tell the shim that a
 * memory range may have changed (DCFlushRange) or be plain no-ops.
 */
#ifndef __OGC_CACHE_H__
#define __OGC_CACHE_H__

#include "../gctypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Implemented in Gx2Core.cpp: tell the shim that guest memory changed (cached converted
 * textures overlapping the range are dropped). The names are remapped with macros because
 * wut's coreinit exports the real DCFlushRange()/DCZeroRange(), which the rest of the app
 * (and the GX2 backend) keep using; only code that includes this header gets the shim.
 */
void gx2gx_DCFlushRange(void *startaddress, u32 len);
void gx2gx_DCStoreRange(void *startaddress, u32 len);
void gx2gx_DCInvalidateRange(void *startaddress, u32 len);
void gx2gx_DCZeroRange(void *startaddress, u32 len);

#define DCFlushRange gx2gx_DCFlushRange
#define DCStoreRange gx2gx_DCStoreRange
#define DCInvalidateRange gx2gx_DCInvalidateRange
#define DCZeroRange gx2gx_DCZeroRange

#ifdef __cplusplus
}
#endif

#endif
