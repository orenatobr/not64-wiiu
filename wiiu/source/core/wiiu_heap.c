/* External-metadata first-fit heap used for the recompiler's code cache.
 *
 * See compat/ogc/lwp_heap.h for why the metadata is kept outside the area.
 * The block list is ordered by address, so a free block can coalesce with both
 * neighbours in O(1) once found. Searches use a roving pointer (next fit).
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "compat/ogc/lwp_heap.h"

struct wiiu_heap_block {
	struct wiiu_heap_block *prev;
	struct wiiu_heap_block *next;
	u32 offset;   /* from heap->start */
	u32 size;     /* in bytes, multiple of heap->align */
	u32 used;
};

static struct wiiu_heap_block *block_new(heap_cntrl *heap)
{
	struct wiiu_heap_block *b = heap->spare;
	if (b) {
		heap->spare = b->next;
	} else {
		b = (struct wiiu_heap_block *)malloc(sizeof(*b));
		if (!b)
			return NULL;
	}
	memset(b, 0, sizeof(*b));
	return b;
}

static void block_recycle(heap_cntrl *heap, struct wiiu_heap_block *b)
{
	b->next = heap->spare;
	b->prev = NULL;
	heap->spare = b;
}

u32 __lwp_heap_init(heap_cntrl *heap, void *area, u32 size, u32 align)
{
	struct wiiu_heap_block *b;

	if (!heap || !area || align == 0 || (align & (align - 1)) != 0)
		return 0;

	size &= ~(align - 1);
	if (size == 0 || ((uintptr_t)area & (align - 1)) != 0)
		return 0;

	memset(heap, 0, sizeof(*heap));
	heap->start = (u8 *)area;
	heap->size = size;
	heap->align = align;

	b = block_new(heap);
	if (!b)
		return 0;
	b->offset = 0;
	b->size = size;
	b->used = 0;
	heap->head = heap->rover = b;
	return size;
}

void __lwp_heap_deinit(heap_cntrl *heap)
{
	struct wiiu_heap_block *b, *n;

	if (!heap)
		return;
	for (b = heap->head; b; b = n) {
		n = b->next;
		free(b);
	}
	for (b = heap->spare; b; b = n) {
		n = b->next;
		free(b);
	}
	memset(heap, 0, sizeof(*heap));
}

void *__lwp_heap_allocate(heap_cntrl *heap, u32 size)
{
	struct wiiu_heap_block *b, *start;

	if (!heap || !heap->head || size == 0)
		return NULL;

	size = (size + heap->align - 1) & ~(heap->align - 1);
	if (size == 0 || size > heap->size)
		return NULL;

	start = b = heap->rover ? heap->rover : heap->head;
	do {
		if (!b->used && b->size >= size) {
			if (b->size > size) {
				struct wiiu_heap_block *rest = block_new(heap);
				if (!rest)
					return NULL;
				rest->offset = b->offset + size;
				rest->size = b->size - size;
				rest->used = 0;
				rest->prev = b;
				rest->next = b->next;
				if (b->next)
					b->next->prev = rest;
				b->next = rest;
				b->size = size;
			}
			b->used = 1;
			heap->rover = b->next ? b->next : heap->head;
			return heap->start + b->offset;
		}
		b = b->next ? b->next : heap->head;
	} while (b != start);

	return NULL;
}

static struct wiiu_heap_block *block_find(heap_cntrl *heap, void *ptr)
{
	struct wiiu_heap_block *b;
	u32 offset;

	if (!heap || !ptr || (u8 *)ptr < heap->start)
		return NULL;
	offset = (u32)((uintptr_t)ptr - (uintptr_t)heap->start);
	if (offset >= heap->size)
		return NULL;

	/* start from the rover side to keep recently used areas cheap */
	for (b = heap->head; b; b = b->next) {
		if (b->offset == offset)
			return b->used ? b : NULL;
		if (b->offset > offset)
			break;
	}
	return NULL;
}

u32 __lwp_heap_block_size(heap_cntrl *heap, void *ptr)
{
	struct wiiu_heap_block *b = block_find(heap, ptr);
	return b ? b->size : 0;
}

BOOL __lwp_heap_free(heap_cntrl *heap, void *ptr)
{
	struct wiiu_heap_block *b = block_find(heap, ptr);

	if (!b)
		return FALSE;
	b->used = 0;

	if (b->next && !b->next->used) {
		struct wiiu_heap_block *n = b->next;
		b->size += n->size;
		b->next = n->next;
		if (n->next)
			n->next->prev = b;
		if (heap->rover == n)
			heap->rover = b;
		block_recycle(heap, n);
	}
	if (b->prev && !b->prev->used) {
		struct wiiu_heap_block *p = b->prev;
		p->size += b->size;
		p->next = b->next;
		if (b->next)
			b->next->prev = p;
		if (heap->rover == b)
			heap->rover = p;
		block_recycle(heap, b);
	}
	return TRUE;
}
