/*
 * gctypes.h (not64 GX2 shim): the libogc integer/float typedefs glN64_GX uses, plus
 * BOOL/TRUE/FALSE as libogc's gctypes.h provides them. BOOL is int32_t, identical to
 * wut's typedef, so including both is fine.
 */
#ifndef __NOT64_GCTYPES_H__
#define __NOT64_GCTYPES_H__

#include <stdint.h>
#include <stddef.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef volatile uint8_t vu8;
typedef volatile uint16_t vu16;
typedef volatile uint32_t vu32;
typedef volatile uint64_t vu64;
typedef volatile int8_t vs8;
typedef volatile int16_t vs16;
typedef volatile int32_t vs32;
typedef volatile int64_t vs64;
typedef float f32;
typedef double f64;
typedef int32_t BOOL;
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
typedef volatile float vf32;
typedef volatile double vf64;

#ifndef ATTRIBUTE_ALIGN
#define ATTRIBUTE_ALIGN(v) __attribute__((aligned(v)))
#endif

#endif
