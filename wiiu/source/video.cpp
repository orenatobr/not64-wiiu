/****************************************************************************
 * not64 for Wii U
 *
 * Based on Snes9x GX by softdev, Michniewski and Daryl Borth.
 *
 * video.cpp
 ***************************************************************************/

#include <string.h>

#include "memmanager.h"
#include "video.h"
#include "fileop.h"
#include "utils/pngcodec.h"

#include "drivers/Platform.h"

GameScreenPng gameScreenPng;

void ClearScreenshot()
{
	if(gameScreenPng.buffer) {
		extmem_free(gameScreenPng.buffer);
		gameScreenPng.buffer = nullptr;
	}

	gameScreenPng.size = 0;
}

/****************************************************************************
 * TakeScreenshot
 *
 * Copies the current emulator frame into a PNG buffer
 ***************************************************************************/
void TakeScreenshot()
{
	if(gameScreenPng.width <= 0 || gameScreenPng.height <= 0)
		return;

	AllocSaveBuffer();
	platform->getVideo()->getEmulatorVideo()->readFrameRGB24(savebuffer);
	uint32_t size = 0;
	ClearScreenshot();
	gameScreenPng.buffer = EncodePNGFromRGB24(gameScreenPng.width, gameScreenPng.height, savebuffer, 0, &size);
	gameScreenPng.size = (int) size;
	FreeSaveBuffer();
}
