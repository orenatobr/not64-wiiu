/* Implementation of the core's DEBUG interface (gui/DEBUG.h) for the Wii U.
 *
 * On the Wii these printed to the screen or a USB Gecko. Here they go to
 * OSReport, which the dborth logger forwards to the debug sinks (UDP, USB
 * serial, Cemu's log). Messages are only formatted when the app turned debug
 * output on, so a release build pays nothing for the calls scattered in the core.
 */
#include <stdio.h>
#include <string.h>

#include <coreinit/debug.h>

#include "../../../gui/DEBUG.h"

char txtbuffer[1024];

static int debug_enabled;

void wiiu_debug_enable(int on)
{
	debug_enabled = on;
}

void DEBUG_print(char *string, int pos)
{
	(void)pos;
	if (debug_enabled && string)
		OSReport("%s", string);
}

void DEBUG_stats(int stats_id, char *info, unsigned int stats_type, unsigned int adjustment_value)
{
	(void)stats_id;
	(void)info;
	(void)stats_type;
	(void)adjustment_value;
}

void DEBUG_update(void)
{
}

char **DEBUG_get_text(void)
{
	static char *empty[1] = { 0 };
	return empty;
}
