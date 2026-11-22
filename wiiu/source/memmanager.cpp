/****************************************************************************
 * not64 for Wii U
 *
 * Based on Snes9x GX, Daryl Borth 2026.
 *
 * memmanager.cpp
 *
 * Memory manager. The Wii U has one large heap, so unlike the Wii and
 * GameCube there is no MEM1/MEM2/ARAM juggling: everything is plain malloc.
 ***************************************************************************/

#include <malloc.h>
#include <string.h>

#include "not64gx.h"
#include "memmanager.h"
#include "filebrowser.h"
#include "fileop.h"

#include "libgui/GuiImageData.h"

void InitMemManager ()
{
	void * decodeScratch = malloc(IMAGE_DECODE_SCRATCH_SIZE);
	browserList = (BROWSERENTRY *)malloc(sizeof(BROWSERENTRY)*MAX_BROWSER_SIZE);

	GuiImageData::setDecodeScratch(decodeScratch, IMAGE_DECODE_SCRATCH_SIZE);
}

void* extmem_malloc(uint32_t size) { return memalign(FILE_BUFFER_ALIGN, size); }
char* extmem_strdup(const char *s) { return strdup(s); }
void extmem_free(void *ptr) { free(ptr); }
int extmem_size_free() { return 0; }

// The browser list stays allocated for the whole run on the Wii U.
void SwitchMemoryModeMenu() { }
void SwitchMemoryModeGame() { }
