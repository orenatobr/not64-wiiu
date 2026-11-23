/* Replacement for libogc's lwp_heap (the recompiler's code cache allocator).
 *
 * Unlike libogc's heap, the bookkeeping lives OUTSIDE the managed area. That
 * matters on the Wii U, where the managed area can be the codegen region: it
 * is either writable or executable but never both, so the allocator must not
 * keep headers inside it.
 */
#ifndef WIIU_COMPAT_LWP_HEAP_H
#define WIIU_COMPAT_LWP_HEAP_H

#include "../gctypes.h"

#ifdef __cplusplus
extern "C" {
#endif

struct wiiu_heap_block;

typedef struct heap_cntrl {
	u8  *start;               /* first byte of the managed area */
	u32  size;                /* size of the managed area in bytes */
	u32  align;               /* allocation granularity and alignment, power of two */
	struct wiiu_heap_block *head;  /* address ordered list of blocks (free and used) */
	struct wiiu_heap_block *rover; /* where the next search starts */
	struct wiiu_heap_block *spare; /* recycled block descriptors */
} heap_cntrl;

/* Returns the number of usable bytes, or 0 on failure. */
u32   __lwp_heap_init(heap_cntrl *heap, void *area, u32 size, u32 align);
void  __lwp_heap_deinit(heap_cntrl *heap);
void *__lwp_heap_allocate(heap_cntrl *heap, u32 size);
/* Returns TRUE when ptr was a live allocation of this heap. */
BOOL  __lwp_heap_free(heap_cntrl *heap, void *ptr);
/* Size in bytes of the live allocation at ptr, or 0 when it is not one. */
u32   __lwp_heap_block_size(heap_cntrl *heap, void *ptr);

#ifdef __cplusplus
}
#endif

#endif
