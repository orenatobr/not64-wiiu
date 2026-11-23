/* gettime()/gettick() and tick conversions for the core, from the Wii U's
 * system timer. The timer runs at a quarter of the bus clock. */
#include <coreinit/systeminfo.h>
#include <coreinit/time.h>

#include "compat/ogc/timesupp.h"

static u64 timer_clock(void)
{
	return (u64)OSGetSystemInfo()->busClockSpeed / 4u;
}

u64 gettime(void)
{
	return (u64)OSGetSystemTime();
}

u32 gettick(void)
{
	return (u32)OSGetSystemTick();
}

u64 wiiu_ticks_to_microsecs(u64 ticks)
{
	return ticks * 1000000u / timer_clock();
}

u64 wiiu_ticks_to_millisecs(u64 ticks)
{
	return ticks * 1000u / timer_clock();
}

u64 wiiu_microsecs_to_ticks(u64 us)
{
	return us * timer_clock() / 1000000u;
}

u64 wiiu_millisecs_to_ticks(u64 ms)
{
	return ms * timer_clock() / 1000u;
}
