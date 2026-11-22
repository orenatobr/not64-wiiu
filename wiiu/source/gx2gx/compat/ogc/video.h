/*
 * ogc/video.h (not64 GX2 shim): the VIDEO_* calls glN64_GX makes. The Wii U video
 * driver owns the display, so these are no-ops (see docs/GX2_RENDERER.md).
 */
#ifndef __OGC_VIDEO_H__
#define __OGC_VIDEO_H__

#include "../gctypes.h"
#include "gx_struct.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*VIRetraceCallback)(u32 retraceCnt);

static inline VIRetraceCallback VIDEO_SetPreRetraceCallback(VIRetraceCallback cb) { (void) cb; return 0; }
static inline VIRetraceCallback VIDEO_SetPostRetraceCallback(VIRetraceCallback cb) { (void) cb; return 0; }
static inline void VIDEO_WaitVSync(void) {}
static inline void VIDEO_Flush(void) {}
static inline void VIDEO_SetNextFramebuffer(void *fb) { (void) fb; }

#ifdef __cplusplus
}
#endif

#endif
