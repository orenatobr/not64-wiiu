/****************************************************************************
 * prof.h
 *
 * Tiny wall-clock profiler for emulator smoke tests (Cemu, CI). Compiled in only
 * when the build defines NOT64_TEST_INPUT, so release builds carry nothing of it.
 * Each slot sums the time between PROF_BEGIN and PROF_END; prof_report() prints
 * the totals and clears them.
 ***************************************************************************/
#pragma once

#ifdef NOT64_TEST_INPUT
#include <stdio.h>
#include <coreinit/debug.h>
#include <coreinit/time.h>

enum { PROF_FRAME, PROF_DLIST, PROF_DRAWS, PROF_PRESENT, PROF_UPLOAD, PROF_COUNT };

#ifdef __cplusplus
extern "C" {
#endif
extern long long prof_total[PROF_COUNT];
extern long long prof_start[PROF_COUNT];
extern int prof_calls[PROF_COUNT];
#ifdef __cplusplus
}
#endif

#define PROF_BEGIN(id) (prof_start[id] = (long long) OSGetSystemTime())
#define PROF_END(id) (prof_total[id] += (long long) OSGetSystemTime() - prof_start[id], prof_calls[id]++)

static inline void prof_report(const char *tag)
{
	static const char *names[PROF_COUNT] = { "frame", "dlist", "draws", "present", "upload" };
	char line[200];
	int n = 0;
	for (int i = 0; i < PROF_COUNT; i++) {
		n += snprintf(line + n, sizeof(line) - n, " %s=%lldms/%d", names[i], (long long) OSTicksToMilliseconds(prof_total[i]), prof_calls[i]);
		prof_total[i] = 0;
		prof_calls[i] = 0;
	}
	OSReport("[n64] prof %s%s\n", tag, line);
}
#else
#define PROF_BEGIN(id) ((void) 0)
#define PROF_END(id) ((void) 0)
#endif
