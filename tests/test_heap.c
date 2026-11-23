/* Host-side unit test for the code cache heap (wiiu/source/core/wiiu_heap.c).
 * Build and run through `make -f Makefile.wiiu test`.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../wiiu/source/core/compat/ogc/lwp_heap.h"

#define AREA_SIZE (64u * 1024u)
#define ALIGN 32u

static heap_cntrl heap;
static uint8_t *area;

static void setup(void)
{
	area = (uint8_t *)aligned_alloc(ALIGN, AREA_SIZE);
	assert(area);
	assert(__lwp_heap_init(&heap, area, AREA_SIZE, ALIGN) == AREA_SIZE);
}

static void teardown(void)
{
	__lwp_heap_deinit(&heap);
	free(area);
}

static void test_init_rejects_bad_arguments(void)
{
	heap_cntrl h;
	uint8_t *a = (uint8_t *)aligned_alloc(ALIGN, 256);
	assert(__lwp_heap_init(&h, a, 256, 24) == 0);      /* alignment not a power of two */
	assert(__lwp_heap_init(&h, a + 1, 255, ALIGN) == 0); /* misaligned area */
	assert(__lwp_heap_init(&h, NULL, 256, ALIGN) == 0);
	assert(__lwp_heap_init(&h, a, 0, ALIGN) == 0);
	free(a);
}

static void test_allocations_are_aligned_and_inside_the_area(void)
{
	setup();
	for (int i = 1; i < 200; i++) {
		uint8_t *p = (uint8_t *)__lwp_heap_allocate(&heap, (u32)i);
		assert(p);
		assert(((uintptr_t)p & (ALIGN - 1)) == 0);
		assert(p >= area && p + i <= area + AREA_SIZE);
	}
	teardown();
}

static void test_exhaustion_returns_null_and_free_recovers(void)
{
	setup();
	void *a = __lwp_heap_allocate(&heap, AREA_SIZE / 2);
	void *b = __lwp_heap_allocate(&heap, AREA_SIZE / 2);
	assert(a && b);
	assert(__lwp_heap_allocate(&heap, ALIGN) == NULL);
	assert(__lwp_heap_free(&heap, a));
	assert(__lwp_heap_allocate(&heap, AREA_SIZE / 4) != NULL);
	teardown();
}

static void test_coalescing_rebuilds_one_big_block(void)
{
	setup();
	void *p[8];
	for (int i = 0; i < 8; i++) {
		p[i] = __lwp_heap_allocate(&heap, AREA_SIZE / 8);
		assert(p[i]);
	}
	/* free in a scattered order, every neighbour combination gets exercised */
	int order[8] = { 3, 1, 5, 0, 7, 2, 6, 4 };
	for (int i = 0; i < 8; i++)
		assert(__lwp_heap_free(&heap, p[order[i]]));
	assert(__lwp_heap_allocate(&heap, AREA_SIZE) == area);
	teardown();
}

static void test_free_rejects_invalid_pointers(void)
{
	setup();
	uint8_t *p = (uint8_t *)__lwp_heap_allocate(&heap, 100);
	assert(p);
	assert(!__lwp_heap_free(&heap, NULL));
	assert(!__lwp_heap_free(&heap, p + ALIGN));       /* not the start of a block */
	assert(!__lwp_heap_free(&heap, area - ALIGN));    /* outside the area */
	assert(__lwp_heap_free(&heap, p));
	assert(!__lwp_heap_free(&heap, p));               /* double free */
	teardown();
}

static void test_block_size_reports_rounded_size(void)
{
	setup();
	void *p = __lwp_heap_allocate(&heap, 33);
	assert(__lwp_heap_block_size(&heap, p) == 64);
	assert(__lwp_heap_block_size(&heap, (uint8_t *)p + ALIGN) == 0);
	teardown();
}

static void test_metadata_is_not_stored_in_the_area(void)
{
	/* The area may be non-writable (codegen region in executable mode):
	 * allocating and freeing must never touch it. */
	setup();
	for (u32 i = 0; i < AREA_SIZE; i++)
		area[i] = 0xA5;
	void *p[16];
	for (int i = 0; i < 16; i++)
		p[i] = __lwp_heap_allocate(&heap, 100 + 7 * (u32)i);
	for (int i = 0; i < 16; i += 2)
		__lwp_heap_free(&heap, p[i]);
	for (int i = 1; i < 16; i += 2)
		__lwp_heap_free(&heap, p[i]);
	for (u32 i = 0; i < AREA_SIZE; i++)
		assert(area[i] == 0xA5);
	teardown();
}

int main(void)
{
	test_init_rejects_bad_arguments();
	test_allocations_are_aligned_and_inside_the_area();
	test_exhaustion_returns_null_and_free_recovers();
	test_coalescing_rebuilds_one_big_block();
	test_free_rejects_invalid_pointers();
	test_block_size_reports_rounded_size();
	test_metadata_is_not_stored_in_the_area();
	puts("test_heap: all tests passed");
	return 0;
}
