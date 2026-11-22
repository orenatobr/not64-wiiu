/* Host-side unit test: ROM loading for the Wii U port.
 *
 * Exercises main/rom_gc.c (header parsing, byte order detection) together with
 * the Wii U ROM cache (wiiu/source/core/ROM-Cache-WiiU.c) and the in-memory
 * ROM source (save_wiiu.c). The three ROM byte orders must all end up as the
 * big-endian (z64) image the core expects.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../main/rom.h"
#include "../main/ROM-Cache.h"
#include "../wiiu/source/core/save_wiiu.h"

extern int rom_byte_swap; /* defined in rom_gc.c, declared under another name in ROM-Cache.h */

/* rom_read() starts the frame timer; the test has no timer. */
void InitTimer(void) {}

#define ROM_SIZE 0x2000

/* A z64 image: magic, then a header with a recognisable name, then a counter pattern. */
static void make_z64(uint8_t *rom)
{
	memset(rom, 0, ROM_SIZE);
	rom[0] = 0x80; rom[1] = 0x37; rom[2] = 0x12; rom[3] = 0x40;
	memcpy(rom + 0x20, "TEST GAME: A/B?     ", 20); /* name field, trailing spaces */
	rom[0x3B] = 'N'; rom[0x3C] = 'T'; rom[0x3E] = 'E'; /* media, id, country */
	for (int i = 0x40; i < ROM_SIZE; i++)
		rom[i] = (uint8_t)(i * 7 + 3);
}

static void to_v64(const uint8_t *z, uint8_t *v)
{
	for (int i = 0; i < ROM_SIZE; i += 2) { v[i] = z[i + 1]; v[i + 1] = z[i]; }
}

static void to_n64(const uint8_t *z, uint8_t *n)
{
	for (int i = 0; i < ROM_SIZE; i += 4) {
		n[i] = z[i + 3]; n[i + 1] = z[i + 2]; n[i + 2] = z[i + 1]; n[i + 3] = z[i];
	}
}

static int load(const uint8_t *image, unsigned size)
{
	fileBrowser_file file;
	wiiu_rom_source_set(image, size, &file);
	int ret = rom_read(&file);
	wiiu_rom_source_clear();
	return ret;
}

static void expect_same_as_z64(const uint8_t *z64, unsigned size)
{
	uint8_t *copy = (uint8_t *)malloc(size);
	assert(copy);
	ROMCache_read(copy, 0, size);
	assert(memcmp(copy, z64, size) == 0);
	free(copy);
}

static void test_all_byte_orders_become_z64(void)
{
	uint8_t z64[ROM_SIZE], v64[ROM_SIZE], n64[ROM_SIZE];
	make_z64(z64);
	to_v64(z64, v64);
	to_n64(z64, n64);

	assert(load(z64, ROM_SIZE) == 0);
	assert(rom_byte_swap == BYTE_SWAP_NONE);
	expect_same_as_z64(z64, ROM_SIZE);
	ROMCache_deinit();

	assert(load(v64, ROM_SIZE) == 0);
	assert(rom_byte_swap == BYTE_SWAP_BYTE);
	expect_same_as_z64(z64, ROM_SIZE);
	ROMCache_deinit();

	assert(load(n64, ROM_SIZE) == 0);
	assert(rom_byte_swap == BYTE_SWAP_HALF);
	expect_same_as_z64(z64, ROM_SIZE);
	ROMCache_deinit();
}

static void test_rejects_files_that_are_not_roms(void)
{
	uint8_t junk[ROM_SIZE];
	memset(junk, 0x55, sizeof(junk));
	assert(load(junk, ROM_SIZE) == ROM_CACHE_INVALID_ROM);

	uint8_t tiny[16] = { 0x80, 0x37, 0x12, 0x40 };
	assert(load(tiny, sizeof(tiny)) == ROM_CACHE_INVALID_ROM);
}

static void test_game_name_is_clean_enough_for_a_file_name(void)
{
	uint8_t z64[ROM_SIZE];
	make_z64(z64);
	assert(load(z64, ROM_SIZE) == 0);

	/* trailing spaces are cut and characters a file name cannot hold are replaced */
	assert(strcmp(ROM_SETTINGS.goodname, "TEST GAME_ A_B_") == 0);
	ROMCache_deinit();
}

static void test_size_not_multiple_of_four_is_safe(void)
{
	uint8_t z64[ROM_SIZE];
	make_z64(z64);
	assert(load(z64, ROM_SIZE - 2) == 0);
	ROMCache_deinit();
}

static void test_reads_past_the_end_are_zero_filled(void)
{
	uint8_t z64[ROM_SIZE], out[64];
	make_z64(z64);
	assert(load(z64, ROM_SIZE) == 0);

	memset(out, 0xFF, sizeof(out));
	ROMCache_read(out, ROM_SIZE - 16, 64);
	assert(memcmp(out, z64 + ROM_SIZE - 16, 16) == 0);
	for (int i = 16; i < 64; i++)
		assert(out[i] == 0);

	memset(out, 0xFF, sizeof(out));
	ROMCache_read(out, ROM_SIZE + 100, 64);
	for (int i = 0; i < 64; i++)
		assert(out[i] == 0);
	ROMCache_deinit();
}

int main(void)
{
	test_all_byte_orders_become_z64();
	test_rejects_files_that_are_not_roms();
	test_game_name_is_clean_enough_for_a_file_name();
	test_size_not_multiple_of_four_is_safe();
	test_reads_past_the_end_are_zero_filled();
	puts("test_rom: all tests passed");
	return 0;
}
