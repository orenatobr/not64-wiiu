/*
 * ogc/machine/processor.h (not64 GX2 shim): only _sync(), used after glN64's
 * dcbz/dcbf texture writes. Espresso is a PowerPC like Broadway, so the same
 * instruction is valid; on other hosts (unit tests) it is a compiler barrier.
 */
#ifndef __OGC_PROCESSOR_H__
#define __OGC_PROCESSOR_H__

#if defined(__powerpc__) || defined(__PPC__)
#define _sync() __asm__ __volatile__("sync" ::: "memory")
#else
#define _sync() __asm__ __volatile__("" ::: "memory")
#endif

#endif
