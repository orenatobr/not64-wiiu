#ifndef WIIU_DEBUG_H
#define WIIU_DEBUG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Turns on the core's DEBUG_print output (sent to OSReport). Off by default. */
void wiiu_debug_enable(int on);

#ifdef __cplusplus
}
#endif

#endif
