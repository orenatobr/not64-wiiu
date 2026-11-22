/* Data / instruction cache helpers (libogc <ogc/cache.h> equivalents).
 *
 * dcbf, dcbst, dcbi-less invalidate, icbi, sync and isync are all usable from
 * user mode on the Espresso. The cache line is 32 bytes, as on the Gekko.
 */
#ifndef WIIU_COMPAT_CACHE_H
#define WIIU_COMPAT_CACHE_H

#include "../gctypes.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIIU_CACHE_LINE 32u

static inline void DCStoreRange(void *start, u32 len)
{
	u32 p = (u32)start & ~(WIIU_CACHE_LINE - 1);
	u32 end = (u32)start + len;
	for (; p < end; p += WIIU_CACHE_LINE)
		__asm__ __volatile__("dcbst 0,%0" : : "r"(p) : "memory");
	__asm__ __volatile__("sync" : : : "memory");
}

static inline void DCFlushRange(void *start, u32 len)
{
	u32 p = (u32)start & ~(WIIU_CACHE_LINE - 1);
	u32 end = (u32)start + len;
	for (; p < end; p += WIIU_CACHE_LINE)
		__asm__ __volatile__("dcbf 0,%0" : : "r"(p) : "memory");
	__asm__ __volatile__("sync" : : : "memory");
}

/* User mode cannot invalidate without writing back (dcbi is privileged), so a
 * "flush" is the closest safe equivalent: memory ends up coherent either way. */
static inline void DCInvalidateRange(void *start, u32 len)
{
	DCFlushRange(start, len);
}

static inline void DCZeroRange(void *start, u32 len)
{
	u32 p = (u32)start & ~(WIIU_CACHE_LINE - 1);
	u32 end = (u32)start + len;
	for (; p < end; p += WIIU_CACHE_LINE)
		__asm__ __volatile__("dcbz 0,%0" : : "r"(p) : "memory");
}

static inline void ICInvalidateRange(void *start, u32 len)
{
	u32 p = (u32)start & ~(WIIU_CACHE_LINE - 1);
	u32 end = (u32)start + len;
	/* The new instructions must reach memory before the icache refetches them. */
	for (u32 q = p; q < end; q += WIIU_CACHE_LINE)
		__asm__ __volatile__("dcbst 0,%0" : : "r"(q) : "memory");
	__asm__ __volatile__("sync" : : : "memory");
	for (; p < end; p += WIIU_CACHE_LINE)
		__asm__ __volatile__("icbi 0,%0" : : "r"(p) : "memory");
	__asm__ __volatile__("sync; isync" : : : "memory");
}

#ifdef __cplusplus
}
#endif

#endif
