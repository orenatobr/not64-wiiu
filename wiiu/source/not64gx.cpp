/****************************************************************************
 * not64 for Wii U
 *
 * Based on Snes9x GX by softdev, crunchy2, Michniewski and Daryl Borth.
 *
 * not64gx.cpp
 *
 * This file controls overall program flow. Most things start and end here!
 ***************************************************************************/

#include "not64gx.h"
#include "emu_n64.h"
#include "video.h"
#include "menu/menu.h"
#include "savemode.h"
#include "preferences.h"
#include "fileop.h"
#include "filebrowser.h"
#include "input.h"
#include "memmanager.h"
#include "font_ttf.h"
#include "libgui/Gui.h"

#include "drivers/Platform.h"
#include "drivers/Thread.h"
#include "drivers/wut/WutPlatform.h"

static WutPlatform platformInstance;
Platform* platform = &platformInstance;

AppRequest appRequest = AppRequest::NONE;
char appPath[1024] = { 0 };
static bool autoboot = false;
static void SaveAppDataBeforeRelease();

int main(int, char *[])
{
	PlatformConfig platformConfig;
	platformConfig.canvasWidth = 640;
	platformConfig.canvasHeight = 480;
	platformConfig.assetScaleX = 3.0f;
	platformConfig.assetScaleY = 2.25f;

	platform->init(platformConfig);
	platform->setSaveHandler(SaveAppDataBeforeRelease);

	InitFileOpThreads();
	InitMemManager();

	fontSystem = new GuiTextRenderer(font_ttf, font_ttf_size, platform->getVideo()->getGlyphRenderer(), platform->getVideo()->getUIScale());

	DefaultSettings();
	ApplySettings();

	if(n64_core_init() != N64_OK)
	{
		// without the core's tables nothing can run: the menu still opens so the user sees why
		ErrorPrompt("Not enough memory to start the emulator.");
	}

	platform->getVideo()->startMenuVideo();
	InitGUIThreads();

	savebuffer = (uint8_t *)extmem_malloc(SAVEBUFFERSIZE);

	while (!platform->shouldExit()) // main loop
	{
		platform->getAudio()->startMenuAudio();

		SetStickPointerEnabled(true); // In the menu the left stick moves the pointer; in the game it does not

		if(!N64IsLoaded())
			MainMenu(MENU_GAMESELECTION);
		else
			MainMenu(MENU_GAME);

		SetStickPointerEnabled(false);

		platform->getAudio()->stopMenuAudio();

		if(platform->shouldExit()) {
			break;
		}

		WaitForBackgroundTasks(15000);

		appRequest = AppRequest::NONE;
		MarkLiveStateChanged();
		platform->getAudio()->startEmulatorAudio();

		N64ApplySettings();

		platform->getVideo()->setFrameTimer(0);
		platform->getVideo()->getEmulatorVideo()->forceVideoUpdate();

		N64Run(); // returns when the menu is requested, or on exit

		platform->getAudio()->stopEmulatorAudio();
		TakeScreenshot();
		MarkScreenshotCurrent();

		// Left for the menu (not for good, which ExitApp() saves for): the first time, there is no Auto Snapshot yet
		if(appRequest == AppRequest::MENU && !platform->isExiting())
			AutoSnapshotOnFirstMenuReturn();

		platform->getVideo()->startMenuVideo();
	} // main loop
	ExitApp();
}

// Everything that has to reach storage before we can go away
// screenshotIsCurrent: the game isn't still running, so its last screenshot is up to date
static void SaveAppData(bool screenshotIsCurrent)
{
	SavePrefsAndWait(); // exit is the one time we wait for settings to reach the device

	AutoSaveOnQuit(screenshotIsCurrent);
}

// Wii U: the OS is about to take the foreground away (HOME menu, power
// button, closing the app) - the last chance to write to storage.
static void SaveAppDataBeforeRelease()
{
	if (platform->isExiting())
		return; // ExitApp() has the foreground and saves for itself

	SaveAppData(false); // the game may still be running
}

void ExitApp()
{
	platform->getInput()->haltRumble();

	// Closed by the OS from the background: SaveAppDataBeforeRelease() has saved, and we can't write any more
	if (platform->getStatus() != Status::Closed)
		SaveAppData(true);

	// Generic safety net: stop and join every Thread still outstanding
	// (device/parse/worker) before any driver it might touch gets torn
	// down inside requestExit()/shutdown().
	Thread::JoinAll();

	platform->requestExit(EmuSettings.exitAction, autoboot);
}
