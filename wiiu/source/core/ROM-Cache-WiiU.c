/* ROM cache for the native Wii U: the whole ROM lives in RAM.
 *
 * The Wii kept a window of the ROM in MEM2 and streamed the rest. The Wii U has
 * about a gigabyte for applications, so every ROM (even the 64 MiB ones, and
 * big hacks) is simply kept fully resident, which removes all cache misses.
 * ROMs are converted to the native big-endian (z64) layout while loading.
 */
#include <malloc.h>
#include <stdlib.h>
#include <string.h>

#include "../../../main/ROM-Cache.h"
#include "../../../main/rom.h"

/* rom.h only declares it on PowerPC; it is defined unconditionally in main/rom_gc.c */
extern int rom_length;

/* The ROM is read in chunks that are a multiple of 4 bytes, so a byte swap
 * never straddles two chunks. */
#define LOAD_CHUNK (1024 * 1024)

static u8 *ROMCache;
static u32 ROMCacheSize;

/* Optional progress callback for the UI, 0.0 .. 1.0. */
void (*wiiu_rom_progress)(float fraction) = NULL;

void ROMCache_init(fileBrowser_file *file)
{
	rom_length = (int)file->size;
	ROMCacheSize = (u32)rom_length;
}

void ROMCache_deinit()
{
	free(ROMCache);
	ROMCache = NULL;
	ROMCacheSize = 0;
}

void *ROMCache_pointer(u32 rom_offset)
{
	return ROMCache + rom_offset;
}

void ROMCache_read(u8 *ram_dest, u32 rom_offset, u32 length)
{
	if (!ROMCache || rom_offset >= ROMCacheSize) {
		memset(ram_dest, 0, length);
		return;
	}
	if (length > ROMCacheSize - rom_offset) {
		u32 avail = ROMCacheSize - rom_offset;
		memcpy(ram_dest, ROMCache + rom_offset, avail);
		memset(ram_dest + avail, 0, length - avail);
		return;
	}
	memcpy(ram_dest, ROMCache + rom_offset, length);
}

void ROMCache_write(u8 *ram_src, u32 rom_offset, u32 length)
{
	if (!ROMCache || rom_offset >= ROMCacheSize)
		return;
	if (length > ROMCacheSize - rom_offset)
		length = ROMCacheSize - rom_offset;
	memcpy(ROMCache + rom_offset, ram_src, length);
}

int ROMCache_load(fileBrowser_file *file)
{
	u32 done = 0;

	if (rom_length < 0x1000)
		return ROM_CACHE_INVALID_ROM;

	free(ROMCache);
	/* Round up so the last partial word can be byte swapped safely. */
	ROMCache = (u8 *)memalign(64, ((u32)rom_length + 63u) & ~63u);
	if (!ROMCache)
		return ROM_CACHE_ERROR_READ;
	memset(ROMCache + (u32)rom_length, 0, (((u32)rom_length + 63u) & ~63u) - (u32)rom_length);

	while (done < (u32)rom_length) {
		u32 want = (u32)rom_length - done;
		int got;

		if (want > LOAD_CHUNK)
			want = LOAD_CHUNK;
		got = romFile_readFile(file, ROMCache + done, want);
		if (got <= 0) {
			ROMCache_deinit();
			return ROM_CACHE_ERROR_READ;
		}

		/* the first word as the big-endian number it spells, whatever the host is */
		if (done == 0 && init_byte_swap(((u32)ROMCache[0] << 24) | ((u32)ROMCache[1] << 16) |
		                                ((u32)ROMCache[2] << 8) | (u32)ROMCache[3]) == BYTE_SWAP_BAD) {
			ROMCache_deinit();
			return ROM_CACHE_INVALID_ROM;
		}

		byte_swap((char *)(ROMCache + done), (unsigned int)got);
		done += (u32)got;

		if (wiiu_rom_progress)
			wiiu_rom_progress((float)done / (float)rom_length);
	}

	return 0;
}
