/****************************************************************************
 * not64 for Wii U
 *
 * Based on Snes9x GX by softdev, crunchy2, Michniewski and Daryl Borth.
 * N64 core: Wii64 / Mupen64GC / not64.
 *
 * not64gx.h
 *
 * This file controls overall program flow. Most things start and end here!
 ***************************************************************************/

#ifndef _NOT64GX_H_
#define _NOT64GX_H_

#include "drivers/InputDriver.h"
#include "drivers/FileSystemDriver.h"
#include "filelist.h"

#define APPNAME 			"not64"
#define APPVERSION 			"0.1.0"
#define APPFOLDER 			"not64"
#define PREF_FILE_NAME		"settings.xml"

#define MAXPATHLEN 1024
#define NOTSILENT 0
#define SILENT 1

enum {
    SAVEFOLDER_SAVES = 0,
    SAVEFOLDER_LENGTH
};

enum {
    LOADFOLDER_ROMS = 0,
    LOADFOLDER_SCREENSHOTS,
    LOADFOLDER_COVERS,
    LOADFOLDER_ARTWORK,
    LOADFOLDER_LENGTH
};

typedef struct {
    int id;
    const char *name;
} FolderDef;

const FolderDef saveFolder[] = {
    { SAVEFOLDER_SAVES,  "saves" }
};

const FolderDef loadFolder[] = {
    { LOADFOLDER_ROMS,        "roms" },
    { LOADFOLDER_SCREENSHOTS, "screenshots" },
    { LOADFOLDER_COVERS,      "covers" },
    { LOADFOLDER_ARTWORK,     "artwork" }
};

enum {
	FILE_SRAM,   // the game's own saves (EEPROM, SRAM, FlashRAM, Controller Pak)
	FILE_STATE,
	FILE_ROM
};

// What is saved and loaded without being asked (see savemode.h)
enum {
	SAVEMODE_SNAPSHOT = 0,	// resume exactly where you left off
	SAVEMODE_GAMESAVE,		// keep the game's own save files; every start is a fresh boot
	SAVEMODE_MANUAL,		// nothing is saved or loaded unless asked
	SAVEMODE_LENGTH
};

enum {
	PREVIEWIMAGE_SCREENSHOT = 0,
	PREVIEWIMAGE_COVER,
	PREVIEWIMAGE_ARTWORK,
	PREVIEWIMAGE_LENGTH
};

enum {
	VIDEO_ASPECT_RATIO_CORRECTION_NONE = 0,   // fill the screen
	VIDEO_ASPECT_RATIO_CORRECTION_4_3,        // keep the N64's 4:3 picture
	VIDEO_ASPECT_RATIO_CORRECTION_4_3_FIXED,  // 4:3 at a whole-number scale (sharpest)
	VIDEO_ASPECT_RATIO_CORRECTION_LENGTH
};

// Internal resolution of the 3D renderer, as a multiple of the N64's 640x480
enum {
	RENDER_SCALE_1X = 0,
	RENDER_SCALE_2X,
	RENDER_SCALE_3X,
	RENDER_SCALE_LENGTH
};

// Which CPU core runs the game (values follow the core's own order)
enum {
	CPUCORE_AUTO = 0,        // the recompiler when this console can run it, else the fastest interpreter
	CPUCORE_DYNAREC,
	CPUCORE_CACHED_INTERPRETER,
	CPUCORE_PURE_INTERPRETER,
	CPUCORE_LENGTH
};

// What the Controller Pak slot of a player holds
enum {
	PAK_NONE = 0,
	PAK_MEMORY,
	PAK_RUMBLE,
	PAK_LENGTH
};

enum {
	GAMEPAD_MENU_TOGGLE_DEFAULT = 0,
	GAMEPAD_MENU_TOGGLE_HOME_RIGHTSTICK,
	GAMEPAD_MENU_TOGGLE_LRSTART_12PLUS,
	GAMEPAD_MENU_TOGGLE_LENGTH
};

// The one kind of controller the N64 has. Kept as an enum so the mapping
// screens (written for several kinds) keep working unchanged.
enum
{
	CTRL_PAD,
	CTRL_LENGTH
};

#define CTRL_BTN_MAPPINGS 1

const char ctrlName[CTRL_LENGTH][24] =
{
	"N64 Controller"
};

enum {
	LANG_JAPANESE = 0,
	LANG_ENGLISH,
	LANG_GERMAN,
	LANG_FRENCH,
	LANG_SPANISH,
	LANG_ITALIAN,
	LANG_DUTCH,
	LANG_SIMP_CHINESE,
	LANG_TRAD_CHINESE,
	LANG_KOREAN,
	LANG_PORTUGUESE,
	LANG_BRAZILIAN_PORTUGUESE,
	LANG_CATALAN,
	LANG_TURKISH,
	LANG_SWEDISH,
	LANG_LENGTH
};

struct SEmuSettings{
	int		saveMode;
	int		loadDevice;
	int		saveDevice;
	char	loadFolder[MAXPATHLEN];
	char	lastFileLoaded[MAXPATHLEN];
	char	saveFolder[MAXPATHLEN];
	char	screenshotsFolder[MAXPATHLEN];
	char	coverFolder[MAXPATHLEN];
	char	artworkFolder[MAXPATHLEN];
	bool	autoloadGame;

	SmbShareInfo smbShare;

	// video
	int		videoAspectRatioCorrection;
	bool	videoBilinearFilter;
	int		videoUpscalingFilter;
	bool	videoScanlines;
	float	videoZoomHor;
	float	videoZoomVert;
	int		videoXshift;
	int		videoYshift;
	int		renderScale;
	bool	frameFramebufferTextures; // glN64: draw the N64 framebuffer into textures (for effects like the Zelda pause screen)

	// emulation
	int		cpuCore;
	int		countPerOp;       // 1 .. 3, how many cycles the core counts per MIPS instruction
	bool	limitFrameRate;
	bool	muteAudio;
	int		pakMode[4];       // Controller Pak of each player, PAK_*

	int		exitAction;
	int		wiimoteOrientation;
	int		musicVolume;
	int		sfxVolume;
	bool	rumble;
	int		language;
	int		previewImage;

	int		gamepadMenuToggle;
	bool	mapRightStickToC; // the right stick presses the C buttons
};

void ExitApp();
extern struct SEmuSettings EmuSettings;
enum class AppRequest { NONE, MENU };
extern AppRequest appRequest;
extern char appPath[];
#endif
