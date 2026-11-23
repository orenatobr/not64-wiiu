/* libogc type names for the not64 core when it is built for the native Wii U.
 *
 * The emulator core was written against libogc (GameCube / Wii). The Wii U
 * build has no libogc, so the handful of types the core uses are provided here.
 * Both CPUs are 32-bit big-endian PowerPC, so the layouts are identical.
 */
#ifndef WIIU_COMPAT_GCTYPES_H
#define WIIU_COMPAT_GCTYPES_H

#include <stdint.h>
#include <stdbool.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

typedef volatile u8  vu8;
typedef volatile u16 vu16;
typedef volatile u32 vu32;
typedef volatile u64 vu64;

typedef volatile s8  vs8;
typedef volatile s16 vs16;
typedef volatile s32 vs32;
typedef volatile s64 vs64;

typedef float  f32;
typedef double f64;

#ifndef BOOL
typedef int BOOL;
#endif
#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifndef ATTRIBUTE_ALIGN
#define ATTRIBUTE_ALIGN(v) __attribute__((aligned(v)))
#endif

#endif
