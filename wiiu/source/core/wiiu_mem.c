/* Run-time replacement for the Wii's fixed MEM2 layout.
 *
 * The core addresses its big tables (TLB lookup tables, block table, save RAM,
 * invalid-code map, ...) through the MEM2_* region macros of gc_memory/MEM2.h.
 * On the Wii those were fixed physical addresses; here they are offsets into
 * one arena allocated once at start-up.
 */
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#include "../../../gc_memory/MEM2.h"
#include "wiiu_mem.h"

char *wiiu_mem2_arena = NULL;

int wiiu_mem2_init(void)
{
	if (wiiu_mem2_arena)
		return 0;

	wiiu_mem2_arena = (char *)memalign(0x1000, MEM2_ARENA_SIZE);
	if (!wiiu_mem2_arena)
		return -1;

	memset(wiiu_mem2_arena, 0, MEM2_ARENA_SIZE);
	return 0;
}

void wiiu_mem2_reset(void)
{
	if (wiiu_mem2_arena)
		memset(wiiu_mem2_arena, 0, MEM2_ARENA_SIZE);
}

void wiiu_mem2_free(void)
{
	free(wiiu_mem2_arena);
	wiiu_mem2_arena = NULL;
}
