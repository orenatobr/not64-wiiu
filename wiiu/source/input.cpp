/****************************************************************************
 * not64 for Wii U
 *
 * Based on Snes9x GX by softdev, crunchy2, Michniewski and Daryl Borth.
 *
 * input.cpp
 *
 * Turns what the Wii U controllers report into N64 controller state.
 ***************************************************************************/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "not64gx.h"
#include "button_mapping.h"
#include "menu/menu.h"
#include "input.h"
#include "libgui/Gui.h"

#include "core/input_wiiu.h"
#include "core/core_wiiu.h"

// The menus and the games share the stick: this is how far it has to move
// before a direction is pressed
#define STICK_PRESS_THRESHOLD 0.55f
// The N64 analog stick reports -80..80 (the real hardware peaks around 85)
#define N64_STICK_RANGE 80.0f
#define STICK_DEADZONE 0.12f

int playerMapping[4] = {0,1,2,3};

uint32_t btnmap[CTRL_BTN_MAPPINGS][INPUT_HW_MAX][MAXJP]; // button mapping

/****************************************************************************
 * Default mappings
 *
 * One row per kind of hardware, in the order of the N64_BTN_* enum:
 * A B Z START L R  C-UP C-DOWN C-LEFT C-RIGHT  D-UP D-DOWN D-LEFT D-RIGHT
 * The analog stick always drives the N64 stick, and when "Right Stick as C
 * Buttons" is on the right stick presses the C buttons on top of this.
 ***************************************************************************/
static void SetMapping(int hw, const uint32_t (&row)[MAXJP])
{
	for(int i = 0; i < MAXJP; i++)
		btnmap[CTRL_PAD][hw][i] = row[i];
}

void ResetControls(int consoleCtrl, int wiiCtrl)
{
	(void)consoleCtrl;

	const uint32_t NONE = INPUT_BTN_NONE;

	// Pro Controller, GamePad, Xbox 360, DualShock 4 and USB pads: face buttons are
	// A and B, the C buttons are X and Y (and the right stick), Z on the triggers
	const uint32_t modern[MAXJP] = {
		INPUT_BTN_A, INPUT_BTN_B, INPUT_TRIGGER_ZL | INPUT_TRIGGER_ZR, INPUT_BTN_PLUS,
		INPUT_TRIGGER_L, INPUT_TRIGGER_R,
		INPUT_BTN_X, NONE, INPUT_BTN_Y, NONE,
		INPUT_BTN_UP, INPUT_BTN_DOWN, INPUT_BTN_LEFT, INPUT_BTN_RIGHT };

	// GameCube controller (adapter): the C stick is the right stick, Z is Z
	const uint32_t gamecube[MAXJP] = {
		INPUT_BTN_A, INPUT_BTN_B, INPUT_TRIGGER_ZR, INPUT_BTN_PLUS,
		INPUT_TRIGGER_L, INPUT_TRIGGER_R,
		INPUT_BTN_X, NONE, INPUT_BTN_Y, NONE,
		INPUT_BTN_UP, INPUT_BTN_DOWN, INPUT_BTN_LEFT, INPUT_BTN_RIGHT };

	// Classic Controller
	const uint32_t classic[MAXJP] = {
		INPUT_BTN_A, INPUT_BTN_B, INPUT_TRIGGER_ZL | INPUT_TRIGGER_ZR, INPUT_BTN_PLUS,
		INPUT_TRIGGER_L, INPUT_TRIGGER_R,
		INPUT_BTN_X, NONE, INPUT_BTN_Y, NONE,
		INPUT_BTN_UP, INPUT_BTN_DOWN, INPUT_BTN_LEFT, INPUT_BTN_RIGHT };

	// Wiimote held sideways: two face buttons, the rest on the D-pad and +/-
	const uint32_t wiimote[MAXJP] = {
		INPUT_BTN_2, INPUT_BTN_1, INPUT_BTN_B, INPUT_BTN_PLUS,
		INPUT_BTN_MINUS, INPUT_BTN_A,
		NONE, NONE, NONE, NONE,
		INPUT_BTN_RIGHT, INPUT_BTN_LEFT, INPUT_BTN_UP, INPUT_BTN_DOWN };

	// Nunchuk + Wiimote: the stick moves, the D-pad is the C buttons
	const uint32_t nunchuk[MAXJP] = {
		INPUT_BTN_A, INPUT_BTN_B, INPUT_TRIGGER_ZL, INPUT_BTN_PLUS,
		INPUT_TRIGGER_L, INPUT_BTN_2,
		INPUT_BTN_UP, INPUT_BTN_DOWN, INPUT_BTN_LEFT, INPUT_BTN_RIGHT,
		NONE, NONE, NONE, NONE };

	// USB mouse: only two buttons, nothing sensible beyond A and B
	const uint32_t mouse[MAXJP] = {
		INPUT_BTN_A, INPUT_BTN_B, NONE, NONE,
		NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE, NONE };

	if(wiiCtrl == -1 || wiiCtrl == INPUT_HW_GAMECUBE) SetMapping(INPUT_HW_GAMECUBE, gamecube);
	if(wiiCtrl == -1 || wiiCtrl == INPUT_HW_WIIMOTE)  SetMapping(INPUT_HW_WIIMOTE, wiimote);
	if(wiiCtrl == -1 || wiiCtrl == INPUT_HW_NUNCHUK)  SetMapping(INPUT_HW_NUNCHUK, nunchuk);
	if(wiiCtrl == -1 || wiiCtrl == INPUT_HW_CLASSIC)  SetMapping(INPUT_HW_CLASSIC, classic);
	if(wiiCtrl == -1 || wiiCtrl == INPUT_HW_WUPC)     SetMapping(INPUT_HW_WUPC, modern);
	if(wiiCtrl == -1 || wiiCtrl == INPUT_HW_DRC)      SetMapping(INPUT_HW_DRC, modern);
	if(wiiCtrl == -1 || wiiCtrl == INPUT_HW_DS4)      SetMapping(INPUT_HW_DS4, modern);
	if(wiiCtrl == -1 || wiiCtrl == INPUT_HW_XBOX360)  SetMapping(INPUT_HW_XBOX360, modern);
	if(wiiCtrl == -1 || wiiCtrl == INPUT_HW_GENERIC_USB) SetMapping(INPUT_HW_GENERIC_USB, modern);
	if(wiiCtrl == -1 || wiiCtrl == INPUT_HW_USBMOUSE) SetMapping(INPUT_HW_USBMOUSE, mouse);
}

/****************************************************************************
 * Analog stick
 *
 * A radial dead zone, then the rest of the throw is stretched to the N64's
 * range so the stick reaches full speed at the edge and starts from zero
 * right after the dead zone (no jump).
 ***************************************************************************/
static int8_t StickToN64(float x, float y, bool horizontal)
{
	const float magnitude = sqrtf(x * x + y * y);
	if(magnitude <= STICK_DEADZONE)
		return 0;

	const float scaled = fminf((magnitude - STICK_DEADZONE) / (1.0f - STICK_DEADZONE), 1.0f);
	const float axis = horizontal ? x : y;
	const float value = (axis / magnitude) * scaled * N64_STICK_RANGE;

	if(value > N64_STICK_RANGE) return (int8_t)N64_STICK_RANGE;
	if(value < -N64_STICK_RANGE) return (int8_t)-N64_STICK_RANGE;
	return (int8_t)lroundf(value);
}

/****************************************************************************
 * decodepad
 *
 * Builds the N64 pad of one port out of everything connected to one Wii U
 * controller channel.
 ***************************************************************************/
static void decodepad(int chan, WiiuN64Pad & out)
{
	if(!controller[chan]) return;
	const InputPadData& pad = controller[chan]->getPadData();

	bool pressed[MAXJP] = { false };
	float stickX = 0.0f, stickY = 0.0f;

	for(uint32_t hw = 0; hw < INPUT_HW_MAX; hw++)
	{
		if(!pad.hw_connected[hw]) continue;

		for(int i = 0; i < MAXJP; i++)
		{
			if(pad.hw_buttons_h[hw] & btnmap[CTRL_PAD][hw][i])
				pressed[i] = true;
		}

		// The right stick can press the C buttons
		if(EmuSettings.mapRightStickToC)
		{
			if(pad.hw_substickY[hw] > STICK_PRESS_THRESHOLD) pressed[N64_BTN_C_UP] = true;
			else if(pad.hw_substickY[hw] < -STICK_PRESS_THRESHOLD) pressed[N64_BTN_C_DOWN] = true;
			if(pad.hw_substickX[hw] < -STICK_PRESS_THRESHOLD) pressed[N64_BTN_C_LEFT] = true;
			else if(pad.hw_substickX[hw] > STICK_PRESS_THRESHOLD) pressed[N64_BTN_C_RIGHT] = true;
		}

		// the strongest stick of all that is connected moves the N64 stick
		if(fabsf(pad.hw_stickX[hw]) + fabsf(pad.hw_stickY[hw]) > fabsf(stickX) + fabsf(stickY))
		{
			stickX = pad.hw_stickX[hw];
			stickY = pad.hw_stickY[hw];
		}
	}

	out.a = pressed[N64_BTN_A];
	out.b = pressed[N64_BTN_B];
	out.z = pressed[N64_BTN_Z];
	out.start = pressed[N64_BTN_START];
	out.l = pressed[N64_BTN_L];
	out.r = pressed[N64_BTN_R];
	out.cup = pressed[N64_BTN_C_UP];
	out.cdown = pressed[N64_BTN_C_DOWN];
	out.cleft = pressed[N64_BTN_C_LEFT];
	out.cright = pressed[N64_BTN_C_RIGHT];
	out.dup = pressed[N64_BTN_D_UP];
	out.ddown = pressed[N64_BTN_D_DOWN];
	out.dleft = pressed[N64_BTN_D_LEFT];
	out.dright = pressed[N64_BTN_D_RIGHT];
	out.x = StickToN64(stickX, stickY, true);
	out.y = StickToN64(stickX, stickY, false);
}

bool isMenuRequested()
{
	for(int i=0; i<4; i++)
	{
		if (!controller[i]) continue;
		const InputPadData& pad = controller[i]->getPadData();

		bool rightStickLeft = (pad.substickX < -0.55f) && !EmuSettings.mapRightStickToC;
		bool homePressed = (pad.buttons_h & INPUT_BTN_HOME);
		bool lPlusRPlusStart = (pad.buttons_h & INPUT_TRIGGER_L) && (pad.buttons_h & INPUT_TRIGGER_R) && (pad.buttons_h & INPUT_BTN_PLUS);
		bool oneTwoPlus = (pad.buttons_h & INPUT_BTN_1) && (pad.buttons_h & INPUT_BTN_2) && (pad.buttons_h & INPUT_BTN_PLUS);

		if (EmuSettings.gamepadMenuToggle == GAMEPAD_MENU_TOGGLE_HOME_RIGHTSTICK)
		{
			if (rightStickLeft || homePressed) return true;
		}
		else if (EmuSettings.gamepadMenuToggle == GAMEPAD_MENU_TOGGLE_LRSTART_12PLUS)
		{
			if (lPlusRPlusStart || oneTwoPlus) return true;
		}
		else // All toggle options enabled
		{
			if (rightStickLeft || homePressed || lPlusRPlusStart || oneTwoPlus) return true;
		}
	}
	return false;
}

static bool buttonsReported = false;

void ClearButtonsReported () {
	buttonsReported = false;
}

/****************************************************************************
 * ReportButtons
 *
 * Called on each emulated frame: polls the hardware, hands the game its
 * controller state, and asks for the menu when the user wants it.
 ***************************************************************************/
void ReportButtons ()
{
	if(buttonsReported)
		return;

	buttonsReported = true;

	platform->getInput()->update();

	/* Check for menu: HOME, the right stick pushed left, L+R+START or 1+2+PLUS */
	if(isMenuRequested())
		appRequest = AppRequest::MENU; // go to the menu

	WiiuN64Pad pads[4];
	memset(pads, 0, sizeof(pads));

	for(int chan = 0; chan < 4; chan++)
	{
		const int port = playerMapping[chan];
		if(port >= 0 && port < 4)
			decodepad(chan, pads[port]);
	}

	for(int port = 0; port < 4; port++)
		wiiu_input_set_pad(port, &pads[port]);
}

/****************************************************************************
 * Rumble
 *
 * The game talks to the Rumble Pak of a port; the pad that plays that port
 * is the one that vibrates.
 ***************************************************************************/
static void OnRumble(int port, int on)
{
	if(!EmuSettings.rumble)
		on = 0;

	for(int chan = 0; chan < 4; chan++)
	{
		if(playerMapping[chan] == port)
			platform->getInput()->setRumble(chan, on != 0);
	}
}

/****************************************************************************
 * IsPadConnected
 *
 * The four logical controllers always exist; one is plugged in when some
 * device (GamePad, Pro Controller, Wiimote, ...) reports itself connected.
 ***************************************************************************/
static bool IsPadConnected(int chan)
{
	if(!controller[chan])
		return false;

	const InputPadData& pad = controller[chan]->getPadData();
	for(uint32_t hw = 0; hw < INPUT_HW_MAX; hw++)
	{
		if(pad.hw_connected[hw])
			return true;
	}
	return false;
}

/****************************************************************************
 * SetControllers
 *
 * Tells the core which ports have a controller and which Pak is in it.
 ***************************************************************************/
void SetControllers()
{
	for(int port = 0; port < 4; port++)
	{
		int pak = N64_PAK_NONE;

		switch(EmuSettings.pakMode[port])
		{
			case PAK_MEMORY: pak = N64_PAK_MEMORY; break;
			case PAK_RUMBLE: pak = N64_PAK_RUMBLE; break;
			default: pak = N64_PAK_NONE; break;
		}

		// A port is "present" when a Wii U controller plays it (player 1 always is)
		bool present = (port == 0);
		for(int chan = 0; chan < 4; chan++)
		{
			if(playerMapping[chan] == port && IsPadConnected(chan))
				present = true;
		}

		wiiu_input_configure_port(port, present, pak);
	}

	wiiu_input_set_rumble_callback(OnRumble);
}

void SetDefaultButtonMap ()
{
	ResetControls();
	SetControllers();
}

const char* GetUSBControllerInfo()
{
    return platform->getInput()->getUsbControllerInfo();
}
