/* libogc time helpers (gettime, gettick, ticks_to_*) on top of the Wii U's
 * OSGetSystemTime(). Implemented in wiiu_time.c. */
#ifndef WIIU_COMPAT_TIMESUPP_H
#define WIIU_COMPAT_TIMESUPP_H

#include "../gctypes.h"

#ifdef __cplusplus
extern "C" {
#endif

u64 gettime(void);
u32 gettick(void);

u64 wiiu_ticks_to_microsecs(u64 ticks);
u64 wiiu_ticks_to_millisecs(u64 ticks);
u64 wiiu_microsecs_to_ticks(u64 us);
u64 wiiu_millisecs_to_ticks(u64 ms);

#define ticks_to_microsecs(t)    wiiu_ticks_to_microsecs((u64)(t))
#define ticks_to_millisecs(t)    wiiu_ticks_to_millisecs((u64)(t))
#define microsecs_to_ticks(us)   wiiu_microsecs_to_ticks((u64)(us))
#define millisecs_to_ticks(ms)   wiiu_millisecs_to_ticks((u64)(ms))

#ifdef __cplusplus
}
#endif

#endif
