#ifndef WIIU_INPUT_H
#define WIIU_INPUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One N64 controller, as the emulated console sees it. */
typedef struct {
	unsigned a      : 1;
	unsigned b      : 1;
	unsigned z      : 1;
	unsigned start  : 1;
	unsigned dup    : 1;
	unsigned ddown  : 1;
	unsigned dleft  : 1;
	unsigned dright : 1;
	unsigned l      : 1;
	unsigned r      : 1;
	unsigned cup    : 1;
	unsigned cdown  : 1;
	unsigned cleft  : 1;
	unsigned cright : 1;
	int8_t x;   /* -80 .. 80, right is positive */
	int8_t y;   /* -80 .. 80, up is positive */
} WiiuN64Pad;

/* Called by the app once per frame for every port. */
void wiiu_input_set_pad(int port, const WiiuN64Pad *pad);

/* pak is PLUGIN_NONE, PLUGIN_MEMPAK or PLUGIN_RUMBLE_PAK (main/plugin.h). */
void wiiu_input_configure_port(int port, int present, int pak);

void wiiu_input_set_rumble_callback(void (*cb)(int port, int on));

#ifdef __cplusplus
}
#endif

#endif
