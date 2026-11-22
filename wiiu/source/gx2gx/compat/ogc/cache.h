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

/* Implemented in Gx2Gx.cpp: invalidates cached converted textures overlapping the range. */
void DCFlushRange(void *startaddress, u32 len);
void DCStoreRange(void *startaddress, u32 len);
void DCInvalidateRange(void *startaddress, u32 len);
void DCZeroRange(void *startaddress, u32 len);

#ifdef __cplusplus
}
#endif

#endif
