/* Controller plugin for the native Wii U.
 *
 * Same plugin ABI as gc_input/input.c (it is linked statically, the macros in
 * InputPlugin.h map the names). The difference is where the pad state comes
 * from: the app reads the Wii U controllers (GamePad, Pro Controller, Wiimote,
 * ...) through dborth's input drivers, translates them to N64 buttons with the
 * user's mapping, and hands the result to wiiu_input_set_pad() once per frame.
 */
#include <string.h>

#include "../../../main/winlnxdefs.h"
#include "../../../gc_input/InputPlugin.h"
#include "../../../gc_input/Controller_#1.1.h"
#include "../../../gc_input/PakIO.h"

#include "input_wiiu.h"

static CONTROL_INFO control_info;
static WiiuN64Pad pad_state[4];
static BOOL pak_last_data[4];
static void (*rumble_cb)(int port, int on);

unsigned char mempack_crc(unsigned char *data);

void wiiu_input_set_pad(int port, const WiiuN64Pad *pad)
{
	if (port >= 0 && port < 4 && pad)
		pad_state[port] = *pad;
}

void wiiu_input_configure_port(int port, int present, int pak)
{
	if (port < 0 || port > 3 || !control_info.Controls)
		return;
	control_info.Controls[port].Present = present ? TRUE : FALSE;
	control_info.Controls[port].RawData = FALSE;
	control_info.Controls[port].Plugin = pak;
}

void wiiu_input_set_rumble_callback(void (*cb)(int port, int on))
{
	rumble_cb = cb;
}

static BYTE writePak(int Control, BYTE *Command)
{
	/* From N-Rage plugin by Norbert Wladyka */
	BYTE *data = &Command[2];
	unsigned int address = (Command[0] << 8) + (Command[1] & 0xE0);

	if (address == PAK_IO_RUMBLE) {
		if (rumble_cb)
			rumble_cb(Control, *data ? 1 : 0);
	} else if (address >= 0x8000 && address < 0x9000) {
		pak_last_data[Control] = (*data) ? TRUE : FALSE;
	}

	data[32] = mempack_crc(data);
	return RD_OK;
}

static BYTE readPak(int Control, BYTE *Command)
{
	BYTE *data = &Command[2];
	unsigned int address = (Command[0] << 8) + (Command[1] & 0xE0);
	int i;

	if (address >= 0x8000 && address < 0x9000 && pak_last_data[Control])
		for (i = 0; i < 32; ++i) data[i] = 0x80;
	else
		for (i = 0; i < 32; ++i) data[i] = 0;

	data[32] = mempack_crc(data);
	return RD_OK;
}

EXPORT void CALL CloseDLL(void)
{
}

EXPORT void CALL ControllerCommand(int Control, BYTE *Command)
{
	/* ReadController follows immediately, nothing to do here. */
	(void)Control;
	(void)Command;
}

EXPORT void CALL DllAbout(HWND hParent)
{
	(void)hParent;
}

EXPORT void CALL DllConfig(HWND hParent)
{
	(void)hParent;
}

EXPORT void CALL DllTest(HWND hParent)
{
	(void)hParent;
}

EXPORT void CALL GetDllInfo(PLUGIN_INFO *PluginInfo)
{
	PluginInfo->Version = 0x0101;
	PluginInfo->Type = PLUGIN_TYPE_CONTROLLER;
	strcpy(PluginInfo->Name, "Wii U input plugin");
}

EXPORT void CALL GetKeys(int Control, BUTTONS *Keys)
{
	const WiiuN64Pad *p;

	if (Control < 0 || Control > 3 || !Keys)
		return;

	p = &pad_state[Control];
	Keys->Value = 0;
	Keys->A_BUTTON = p->a;
	Keys->B_BUTTON = p->b;
	Keys->Z_TRIG = p->z;
	Keys->START_BUTTON = p->start;
	Keys->U_DPAD = p->dup;
	Keys->D_DPAD = p->ddown;
	Keys->L_DPAD = p->dleft;
	Keys->R_DPAD = p->dright;
	Keys->L_TRIG = p->l;
	Keys->R_TRIG = p->r;
	Keys->U_CBUTTON = p->cup;
	Keys->D_CBUTTON = p->cdown;
	Keys->L_CBUTTON = p->cleft;
	Keys->R_CBUTTON = p->cright;
	Keys->X_AXIS = p->x;
	Keys->Y_AXIS = p->y;
}

EXPORT void CALL InitiateControllers(CONTROL_INFO ControlInfo)
{
	int i;

	control_info = ControlInfo;
	for (i = 0; i < 4; i++) {
		control_info.Controls[i].Present = FALSE;
		control_info.Controls[i].RawData = FALSE;
		control_info.Controls[i].Plugin = PLUGIN_NONE;
	}
}

EXPORT void CALL ReadController(int Control, BYTE *Command)
{
	if (Control < 0 || Control > 3 || !Command)
		return;

	/* From N-Rage plugin by Norbert Wladyka */
	switch (Command[2]) {
	case RD_RESETCONTROLLER:
	case RD_GETSTATUS:
		/* expected: the controller gets 1 byte (command), sends back 3 bytes */
		Command[3] = RD_GAMEPAD | RD_ABSOLUTE;
		Command[4] = RD_NOEEPROM;
		if (control_info.Controls[Control].Present)
			Command[5] = (control_info.Controls[Control].Plugin != PLUGIN_NONE)
				? RD_PLUGIN : RD_NOPLUGIN;
		else
			Command[5] = RD_NOPLUGIN | RD_NOTINITIALIZED;
		break;
	case RD_READKEYS:
		GetKeys(Control, (BUTTONS *)&Command[3]);
		break;
	case RD_READPAK:
		readPak(Control, &Command[3]);
		break;
	case RD_WRITEPAK:
		writePak(Control, &Command[3]);
		break;
	case RD_READEEPROM:
	case RD_WRITEEPROM:
		/* handled by the emulator core */
		break;
	default:
		/* only reachable with a buggy or unusual ROM */
		Command[1] = Command[1] | RD_ERROR;
	}
}

EXPORT void CALL RomClosed(void)
{
	memset(pad_state, 0, sizeof(pad_state));
	memset(pak_last_data, 0, sizeof(pak_last_data));
}

EXPORT void CALL RomOpen(void)
{
}

EXPORT void CALL WM_KeyDown(WPARAM wParam, LPARAM lParam)
{
	(void)wParam;
	(void)lParam;
}

EXPORT void CALL WM_KeyUp(WPARAM wParam, LPARAM lParam)
{
	(void)wParam;
	(void)lParam;
}
