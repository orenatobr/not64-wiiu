/*
 * ogc/lwp_heap.h (not64 GX2 shim): the tiny part of libogc's heap API that
 * glN64_GX uses for its texture cache. This is a budgeted malloc: the area
 * handed to __lwp_heap_init only sets the byte budget (the memory itself is
 * not used), allocations come from memalign() and are rejected once the
 * budget is spent, so glN64's "free the oldest texture and retry" loop keeps
 * working exactly like with the fixed MEM2 area.
 */
#ifndef __OGC_LWP_HEAP_H__
#define __OGC_LWP_HEAP_H__

#include "../gctypes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _heap_cntrl {
	u32 budget;
	u32 used;
	u32 pg_size;
} heap_cntrl;

u32 __lwp_heap_init(heap_cntrl *theheap, void *start_addr, u32 size, u32 pg_size);
void *__lwp_heap_allocate(heap_cntrl *theheap, u32 size);
int __lwp_heap_free(heap_cntrl *theheap, void *ptr);

#ifdef __cplusplus
}
#endif

#endif
