/* The few bits of libogc's <ogc/machine/processor.h> the not64 core uses.
 *
 * Byte-reversed loads and stores are single PowerPC instructions that work in
 * user mode on the Espresso (Wii U) exactly as on the Broadway (Wii).
 */
#ifndef WIIU_COMPAT_PROCESSOR_H
#define WIIU_COMPAT_PROCESSOR_H

#include "../../gctypes.h"

static inline u32 __lwbrx(const void *base, u32 index)
{
	u32 res;
	__asm__ __volatile__("lwbrx %0,%1,%2" : "=r"(res) : "b"(index), "r"(base) : "memory");
	return res;
}

static inline u16 __lhbrx(const void *base, u32 index)
{
	u16 res;
	__asm__ __volatile__("lhbrx %0,%1,%2" : "=r"(res) : "b"(index), "r"(base) : "memory");
	return res;
}

static inline void __stwbrx(void *base, u32 index, u32 value)
{
	__asm__ __volatile__("stwbrx %0,%1,%2" : : "r"(value), "b"(index), "r"(base) : "memory");
}

static inline void __sthbrx(void *base, u32 index, u16 value)
{
	__asm__ __volatile__("sthbrx %0,%1,%2" : : "r"(value), "b"(index), "r"(base) : "memory");
}

#endif
