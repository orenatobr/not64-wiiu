/****************************************************************************
 * not64 for Wii U
 *
 * emu_n64.cpp
 ***************************************************************************/

#include <stdio.h>
#include <string.h>

#include "not64gx.h"
#include "emu_n64.h"
#include "fileop.h"
#include "filebrowser.h"
#include "input.h"
#include "menu/menu.h"
#include "video.h"
#include "drivers/Platform.h"
#include "drivers/safestr.h"
#include "libgui/Gui.h"
#include "gx2gx/Gx2Gx.h"

N64RomInfo n64Rom;
char n64RomName[512];
static bool romLoaded = false;

bool N64IsLoaded()
{
	return romLoaded;
}

/****************************************************************************
 * Folders
 ***************************************************************************/
static bool SaveFolderPath(char * out, size_t outSize)
{
	if(EmuSettings.saveDevice == DEVICE_AUTO)
		return false;

	return platform->getFileSystem()->getPath(out, outSize, EmuSettings.saveDevice, EmuSettings.saveFolder);
}

// Makes sure the folder exists and tells the core where it is
static bool PrepareFolders(char * saveDir, size_t size)
{
	if(!SaveFolderPath(saveDir, size))
		return false;

	CreateDirectory(saveDir);
	n64_state_set_folder(saveDir);
	return true;
}

/****************************************************************************
 * The renderer
 *
 * GX2 keeps its context state and command buffer per core, so the renderer has to be
 * created by the thread that draws with it: the one that runs the game. The ROM is
 * opened on a loader thread, which is why this is not done in N64OpenRom(). Its
 * resolution follows the setting, so it is rebuilt when that changed.
 ***************************************************************************/
// height in pixels of the picture the TV gets (720 or 1080), see WutVideoDriver.cpp
extern "C" int Wiiu_TvHeight(void);

static bool EnsureRenderer()
{
	static int rendererScale = 0;
	int wantedScale = EmuSettings.renderScale + 1;
	if(EmuSettings.renderScale == RENDER_SCALE_AUTO)
	{
		// The N64 picture is 4:3 and fills the TV's height: render at least as many lines as the TV shows
		// (720p: 960x720 on screen, so 2x = 960 lines; 1080p: 1440x1080, so 3x = 1440 lines)
		const int tvHeight = Wiiu_TvHeight();
		wantedScale = tvHeight > 720 ? 3 : (tvHeight > 480 ? 2 : 1);
	}

	if(rendererScale == wantedScale)
		return true;

	if(rendererScale != 0)
		gx2gx::shutdown();

	if(!gx2gx::init(wantedScale))
	{
		rendererScale = 0;
		ErrorPrompt("The graphics renderer could not start.");
		return false;
	}

	rendererScale = wantedScale;
	return true;
}

/****************************************************************************
 * The game
 ***************************************************************************/
bool N64OpenRom(const uint8_t * data, size_t size, const char * fileStem)
{
	if(size > 0xFFFFFFFFu)
	{
		ErrorPrompt("The ROM is too big!");
		return false;
	}

	N64CloseRom();

	N64RomInfo info;
	const int result = n64_load_rom(data, (unsigned int)size, &info);

	switch(result)
	{
		case N64_OK:
			break;
		case N64_ERR_INVALID_ROM:
			ErrorPrompt("This is not an N64 ROM.");
			return false;
		case N64_ERR_MEMORY:
			ErrorPrompt("Not enough memory to load this ROM.");
			return false;
		default:
			ErrorPrompt("The ROM could not be loaded.");
			return false;
	}

	n64Rom = info;
	CopyString(n64RomName, sizeof(n64RomName), fileStem ? fileStem : info.name);
	romLoaded = true;

	N64ApplySettings();
	return true;
}

void N64CloseRom()
{
	if(!romLoaded)
		return;

	n64_unload_rom();
	romLoaded = false;
	memset(&n64Rom, 0, sizeof(n64Rom));
	n64RomName[0] = 0;
}

void N64ApplySettings()
{
	int core = N64_CORE_PURE_INTERPRETER;

	switch(EmuSettings.cpuCore)
	{
		case CPUCORE_DYNAREC:             core = N64_CORE_DYNAREC; break;
		case CPUCORE_CACHED_INTERPRETER:  core = N64_CORE_CACHED_INTERPRETER; break;
		case CPUCORE_PURE_INTERPRETER:    core = N64_CORE_PURE_INTERPRETER; break;
		case CPUCORE_AUTO:
		default:
			core = n64_dynarec_available() ? N64_CORE_DYNAREC : N64_CORE_PURE_INTERPRETER;
			break;
	}

	n64_set_cpu_core(core);
	n64_set_count_per_op(EmuSettings.countPerOp);
	n64_set_audio_enabled(!EmuSettings.muteAudio);
	n64_set_frame_limit(EmuSettings.limitFrameRate);
	n64_set_framebuffer_textures(EmuSettings.frameFramebufferTextures);
	SetControllers();
}

/****************************************************************************
 * Running
 *
 * The core calls OnFrame once per emulated video frame, on this thread. That
 * is where input is read and where a request to leave the game is noticed.
 ***************************************************************************/
static void OnFrame()
{
	SystemEvent event = platform->getSystemEvent(); // poll exactly once per frame

	if(platform->isExiting() || event == SystemEvent::ShutdownRequested)
	{
		n64_request_stop();
		return;
	}

	ReportButtons();
	ClearButtonsReported();

	if(appRequest != AppRequest::NONE)
		n64_request_stop();
}

void N64Run()
{
	if(!romLoaded || !EnsureRenderer())
		return;

	n64_set_frame_callback(OnFrame);
	n64_run();
	n64_set_frame_callback(nullptr);
}

void N64Reset()
{
	if(!romLoaded)
		return;

	if(n64_reset() != N64_OK)
		ErrorPrompt("The game could not be reset.");
}

/****************************************************************************
 * Saves
 ***************************************************************************/
bool N64LoadGameSaves(bool silent)
{
	char dir[MAXPATHLEN];

	if(!romLoaded || !PrepareFolders(dir, sizeof(dir)))
		return false;

	const int found = n64_saves_load(dir);
	if(found > 0 && !silent)
		InfoPrompt("Game save loaded.");

	return found > 0;
}

bool N64SaveGameSaves(bool silent)
{
	char dir[MAXPATHLEN];

	if(!romLoaded)
		return false;

	if(!PrepareFolders(dir, sizeof(dir)))
	{
		if(!silent)
			ErrorPrompt("No device to save to.");
		return false;
	}

	if(!n64_saves_dirty())
		return true; // nothing changed: what is on the device is current

	const int written = n64_saves_save(dir);
	if(written < 0)
	{
		if(!silent)
			ErrorPrompt("The game save could not be written.");
		return false;
	}
	return true;
}

bool N64GameSavesDirty()
{
	return romLoaded && n64_saves_dirty();
}

/****************************************************************************
 * States
 ***************************************************************************/
bool N64StatePath(char * out, size_t outSize, int slot)
{
	char dir[MAXPATHLEN];

	if(!romLoaded || slot < 0 || slot > 9 || !SaveFolderPath(dir, sizeof(dir)))
		return false;

	return FormatString(out, outSize, "%s/%s%s.st%d", dir, n64Rom.name, n64Rom.region, slot);
}

bool N64StateExists(int slot)
{
	char dir[MAXPATHLEN];

	if(!romLoaded || !PrepareFolders(dir, sizeof(dir)))
		return false;

	return n64_state_exists(slot) != 0;
}

bool N64SaveState(int slot, bool silent)
{
	char dir[MAXPATHLEN];

	if(!romLoaded)
		return false;

	if(!PrepareFolders(dir, sizeof(dir)))
	{
		if(!silent)
			ErrorPrompt("No device to save to.");
		return false;
	}

	if(n64_state_save(slot) != N64_OK)
	{
		if(!silent)
			ErrorPrompt("The state could not be saved.");
		return false;
	}

	// the screenshot of the moment the menu was opened is the state's picture
	char path[MAXPATHLEN];
	if(gameScreenPng.buffer && gameScreenPng.size > 0 && N64StatePath(path, sizeof(path) - 8, slot))
	{
		strncat(path, ".png", sizeof(path) - strlen(path) - 1);
		memcpy(savebuffer, gameScreenPng.buffer, (size_t)gameScreenPng.size);
		SaveFile((char *)savebuffer, path, (size_t)gameScreenPng.size, SILENT);
	}
	return true;
}

bool N64LoadState(int slot, bool silent)
{
	char dir[MAXPATHLEN];

	if(!romLoaded || !PrepareFolders(dir, sizeof(dir)))
		return false;

	if(!n64_state_exists(slot))
	{
		if(!silent)
			ErrorPrompt("There is no state in this slot.");
		return false;
	}

	if(n64_state_load(slot) != N64_OK)
	{
		if(!silent)
			ErrorPrompt("The state could not be loaded.");
		return false;
	}
	return true;
}
