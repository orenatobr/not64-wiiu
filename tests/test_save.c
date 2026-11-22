/* Host-side unit test: the stdio save backend used for EEPROM, SRAM, FlashRAM
 * and Controller Pak files (wiiu/source/core/save_wiiu.c). */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../wiiu/source/core/save_wiiu.h"

static char dir[128];

static void path_in_dir(fileBrowser_file *f, const char *name)
{
	memset(f, 0, sizeof(*f));
	snprintf(f->name, sizeof(f->name), "%s/%s", dir, name);
}

static void test_round_trip(void)
{
	fileBrowser_file f;
	unsigned char data[0x800], back[0x800];
	for (unsigned i = 0; i < sizeof(data); i++)
		data[i] = (unsigned char)(i * 13);

	wiiu_save_backend_install();
	path_in_dir(&f, "game.eep");
	assert(saveFile_writeFile(&f, data, sizeof(data)) == (int)sizeof(data));

	path_in_dir(&f, "game.eep");
	assert(saveFile_readFile(&f, back, sizeof(back)) == (int)sizeof(back));
	assert(memcmp(data, back, sizeof(data)) == 0);
}

static void test_missing_file_reports_an_error(void)
{
	fileBrowser_file f;
	unsigned char buf[16];
	path_in_dir(&f, "does-not-exist.sra");
	assert(saveFile_readFile(&f, buf, sizeof(buf)) < 0);
}

static void test_overwrite_replaces_and_leaves_no_temporary_file(void)
{
	fileBrowser_file f;
	unsigned char first[64], second[32], back[64];
	memset(first, 0xAA, sizeof(first));
	memset(second, 0x55, sizeof(second));

	path_in_dir(&f, "game.sra");
	assert(saveFile_writeFile(&f, first, sizeof(first)) == (int)sizeof(first));
	path_in_dir(&f, "game.sra");
	assert(saveFile_writeFile(&f, second, sizeof(second)) == (int)sizeof(second));

	path_in_dir(&f, "game.sra");
	assert(saveFile_readFile(&f, back, sizeof(back)) == (int)sizeof(second));
	assert(memcmp(back, second, sizeof(second)) == 0);

	char tmp[160];
	snprintf(tmp, sizeof(tmp), "%s/game.sra.tmp", dir);
	assert(access(tmp, F_OK) != 0);
}

static void test_failed_write_keeps_the_old_save(void)
{
	fileBrowser_file f;
	unsigned char keep[32], back[32];
	memset(keep, 0x11, sizeof(keep));

	path_in_dir(&f, "game.fla");
	assert(saveFile_writeFile(&f, keep, sizeof(keep)) == (int)sizeof(keep));

	/* a folder that does not exist: the write fails, the file above stays */
	memset(&f, 0, sizeof(f));
	snprintf(f.name, sizeof(f.name), "%s/missing-folder/game.fla", dir);
	assert(saveFile_writeFile(&f, keep, sizeof(keep)) < 0);

	path_in_dir(&f, "game.fla");
	assert(saveFile_readFile(&f, back, sizeof(back)) == (int)sizeof(back));
	assert(memcmp(keep, back, sizeof(keep)) == 0);
}

static void test_rom_source_serves_the_bytes_it_was_given(void)
{
	unsigned char rom[100], out[40];
	fileBrowser_file f;
	for (unsigned i = 0; i < sizeof(rom); i++)
		rom[i] = (unsigned char)i;

	wiiu_rom_source_set(rom, sizeof(rom), &f);
	assert(romFile_init(&f) == 0);
	assert(f.size == sizeof(rom));

	assert(romFile_readFile(&f, out, 40) == 40);
	assert(memcmp(out, rom, 40) == 0);
	assert(romFile_readFile(&f, out, 40) == 40);
	assert(memcmp(out, rom + 40, 40) == 0);
	assert(romFile_readFile(&f, out, 40) == 20);   /* only 20 left */
	assert(romFile_readFile(&f, out, 40) == 0);    /* at the end */

	assert(romFile_seekFile(&f, 10, FILE_BROWSER_SEEK_SET) == 0);
	assert(romFile_readFile(&f, out, 4) == 4 && out[0] == 10);
	assert(romFile_seekFile(&f, 1000, FILE_BROWSER_SEEK_SET) < 0);
	wiiu_rom_source_clear();
}

int main(void)
{
	snprintf(dir, sizeof(dir), "/tmp/not64-test-save-%d", (int)getpid());
	assert(mkdir(dir, 0700) == 0);

	test_round_trip();
	test_missing_file_reports_an_error();
	test_overwrite_replaces_and_leaves_no_temporary_file();
	test_failed_write_keeps_the_old_save();
	test_rom_source_serves_the_bytes_it_was_given();

	char cmd[200];
	snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
	if (system(cmd) != 0)
		return 1;

	puts("test_save: all tests passed");
	return 0;
}
