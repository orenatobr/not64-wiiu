/****************************************************************************
 * not64 for Wii U
 *
 * Based on Snes9x GX by softdev, Michniewski and Daryl Borth.
 *
 * video.h
 *
 * Screenshot of the game frame, used for previews and as the menu backdrop.
 ***************************************************************************/

#ifndef _GCVIDEOH_
#define _GCVIDEOH_

#include <stdint.h>

typedef struct
{
	uint8_t * buffer; // PNG data
	int size;
	int width;
	int height;
	float scaleX;
	float scaleY;
	int xoffset;
	int yoffset;
} GameScreenPng;

extern GameScreenPng gameScreenPng;

void ClearScreenshot();
void TakeScreenshot();

#endif
