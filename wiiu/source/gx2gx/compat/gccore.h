/*
 * gccore.h (not64 GX2 shim): on Wii U this replaces libogc's umbrella header
 * for glN64_GX. It only exposes what glN64_GX uses: the GX API and constants
 * (implemented on GX2, see Gx2Gx.h), the gu math helpers, cache helpers, the
 * GXRModeObj struct and no-op VIDEO_* stubs.
 */
#ifndef __NOT64_GCCORE_H__
#define __NOT64_GCCORE_H__

#include "gctypes.h"
#include "ogc/gx.h"
#include "ogc/gu.h"
#include "ogc/cache.h"
#include "ogc/video.h"
#include "ogc/lwp_heap.h"
#include "ogc/machine/processor.h"

#endif
