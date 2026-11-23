/****************************************************************************
 * not64 for Wii U
 *
 * Based on Snes9x GX, Daryl Borth 2008-2026
 *
 * Daryl Borth 2008-2026
 *
 * savemode.cpp
 *
 * Save Mode: what is saved and loaded without being asked (see savemode.h)
 ***************************************************************************/

#include <stdio.h>
#include <string.h>

#include "not64gx.h"
#include "savemode.h"
#include "emu_n64.h"
#include "fileop.h"
#include "filebrowser.h"
#include "menu/menu.h"

// What is in storage is behind what is in the machine, for each of the two autosaves. They are kept apart
// because they are written apart: the Save Mode can change in the menu, and either file can be updated on
// request, which brings only that one up to date.
static bool snapshotStale = false;
static bool cartridgeStale = false;

// The screenshot taken when the game was left no longer shows the machine
static bool screenshotStale = false;

void MarkLiveStateChanged()
{
	snapshotStale = true;
	cartridgeStale = true;
	screenshotStale = true;
}

void ClearLiveStateChanged()
{
	snapshotStale = false;
	cartridgeStale = false;
}

bool LiveStateChanged()
{
	switch(EmuSettings.saveMode)
	{
		case SAVEMODE_SNAPSHOT:
			return snapshotStale;
		case SAVEMODE_GAMESAVE:
			return cartridgeStale;
		default:
			return false;
	}
}

void AutoSnapshotWritten()
{
	snapshotStale = false;
}

void CartridgeSaveWritten()
{
	cartridgeStale = false;
}

void MarkScreenshotCurrent()
{
	screenshotStale = false;
}

bool ScreenshotIsCurrent()
{
	return !screenshotStale;
}

/****************************************************************************
 * AutoLoadOnGameOpen
 *
 * The game has just been loaded and is cold booted. Runs on the thread that
 * loaded it.
 *
 * The game's own saves (EEPROM, SRAM, FlashRAM, Controller Pak) are loaded in
 * Snapshot and Cartridge Save mode. A Snapshot is a full machine state and does
 * not hold them, so it is applied on top.
 ***************************************************************************/
void AutoLoadOnGameOpen()
{
	switch(EmuSettings.saveMode)
	{
		case SAVEMODE_SNAPSHOT:
		{
			N64LoadGameSaves(true);

			if(N64StateExists(0) && !N64LoadState(0, true))
				ErrorPrompt("The Auto Snapshot couldn't be loaded. The game starts from the beginning.");
			break;
		}

		case SAVEMODE_GAMESAVE:
			N64LoadGameSaves(true);
			break;

		case SAVEMODE_MANUAL:
		default:
			break;
	}

	ClearLiveStateChanged(); // what was just loaded is what is in storage
}

/****************************************************************************
 * AutoSaveOnQuit
 *
 * Leaving the game for good. The game's own saves are written whatever the
 * Save Mode is when the game changed them, so a battery save is never lost.
 * In Snapshot mode what the game was doing is written too.
 ***************************************************************************/
void AutoSaveOnQuit(bool screenshotIsCurrent)
{
	(void)screenshotIsCurrent;

	if(!N64IsLoaded())
		return;

	const bool inMenu = appRequest == AppRequest::MENU;

	// the game's own saves first: they are small and what matters most
	WaitForBackgroundTasks(15000);
	N64SaveGameSaves(true);
	CartridgeSaveWritten();

	if(EmuSettings.saveMode != SAVEMODE_SNAPSHOT)
		return;

	if(inMenu && !snapshotStale)
		return;

	N64SaveState(0, true);
	AutoSnapshotWritten();
}

/****************************************************************************
 * AutoSnapshotOnFirstMenuReturn
 *
 * The game has just been left for the menu. Returning to the menu doesn't
 * write the Auto Snapshot (see savemode.h) - but with none at all, the Saves
 * screen would say "Empty" until the game was left for good. So the first
 * time, with no Auto Snapshot on the device, it is written now.
 ***************************************************************************/
void AutoSnapshotOnFirstMenuReturn()
{
	if(!N64IsLoaded() || EmuSettings.saveMode != SAVEMODE_SNAPSHOT)
		return;

	if(N64StateExists(0))
		return; // there is one: leave it as it is

	if(N64SaveState(0, true))
		AutoSnapshotWritten();
}
