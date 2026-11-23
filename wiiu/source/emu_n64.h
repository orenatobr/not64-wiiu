/****************************************************************************
 * not64 for Wii U
 *
 * emu_n64.h
 *
 * The app's view of the N64 emulator: one loaded game, its saves and states,
 * and a call that runs it. Everything the menus used to ask Snes9x for goes
 * through here.
 ***************************************************************************/

#ifndef _EMU_N64_H_
#define _EMU_N64_H_

#include <stddef.h>
#include <stdint.h>

#include "core/core_wiiu.h"

// The game that is loaded (valid while N64IsLoaded())
extern N64RomInfo n64Rom;
// File name of the ROM without its extension. It names the game in the UI and
// keys everything the app stores for it (previews, state screenshots).
extern char n64RomName[512];

bool N64IsLoaded();

// Gives the ROM image (z64, v64 or n64 byte order) to the core. The buffer is not kept.
// Returns false after telling the user what went wrong.
bool N64OpenRom(const uint8_t * data, size_t size, const char * fileStem);
void N64CloseRom();

// Pushes the current settings (CPU core, timing, Controller Paks, frame limiter) into the core
void N64ApplySettings();

// Runs the game on this thread until it is stopped (menu requested, exit, power off)
void N64Run();
void N64Reset();

// The game's own saves (EEPROM, SRAM, FlashRAM, Controller Pak)
bool N64LoadGameSaves(bool silent);
bool N64SaveGameSaves(bool silent);
bool N64GameSavesDirty();

// Save states, ten slots per game. Slot 0 is the Auto Snapshot.
bool N64StateExists(int slot);
bool N64SaveState(int slot, bool silent);
bool N64LoadState(int slot, bool silent);
// Path of the file a state is stored in, and of its screenshot
bool N64StatePath(char * out, size_t outSize, int slot);

#endif
