/****************************************************************************
 * not64 for Wii U
 *
 * Based on Snes9x GX by softdev, crunchy2, Michniewski and Daryl Borth.
 *
 * input.h
 ***************************************************************************/

#ifndef _INPUT_H_
#define _INPUT_H_

#include <stdint.h>

#include "drivers/InputData.h"
#include "not64gx.h"

// The mappable N64 controls, in the order of the mapping tables
enum {
	N64_BTN_A = 0,
	N64_BTN_B,
	N64_BTN_Z,
	N64_BTN_START,
	N64_BTN_L,
	N64_BTN_R,
	N64_BTN_C_UP,
	N64_BTN_C_DOWN,
	N64_BTN_C_LEFT,
	N64_BTN_C_RIGHT,
	N64_BTN_D_UP,
	N64_BTN_D_DOWN,
	N64_BTN_D_LEFT,
	N64_BTN_D_RIGHT,
	N64_BTN_COUNT
};

#define MAXJP N64_BTN_COUNT // # of mappable controller buttons

// btnmap[kind][hardware][N64 button] is the set of hardware buttons that press it
extern uint32_t btnmap[CTRL_BTN_MAPPINGS][INPUT_HW_MAX][MAXJP];
// playerMapping[wii u controller] is the N64 port it plays
extern int playerMapping[4];

void ResetControls(int cc = -1, int wc = -1);
// Polls the controllers and gives the game its pad state. Once per frame.
void ReportButtons ();
void ClearButtonsReported ();
void SetControllers ();
void SetDefaultButtonMap ();
bool isMenuRequested();
const char* GetUSBControllerInfo();

#endif
