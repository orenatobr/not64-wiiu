/* Wii U backends for the core's file callbacks (see fileBrowser/fileBrowser.h).
 *
 * The core never opens files itself. It asks the romFile_* callbacks for the
 * ROM and the saveFile_* callbacks for EEPROM, SRAM, FlashRAM and Controller
 * Pak data. Here the ROM comes from a buffer the app already loaded (so zip,
 * 7z and SMB sources all work the same), and saves go through stdio, which the
 * Wii U drivers map onto the SD card, USB drives and so on.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "../../../fileBrowser/fileBrowser.h"
#include "save_wiiu.h"

/* ---- definitions of the callback slots declared in fileBrowser.h ---- */

fileBrowser_file *romFile_topLevel;
fileBrowser_file *saveFile_dir;

int (*romFile_init)(fileBrowser_file *) = NULL;
int (*romFile_readDir)(fileBrowser_file *, fileBrowser_file **) = NULL;
int (*romFile_readFile)(fileBrowser_file *, void *, unsigned int) = NULL;
int (*romFile_seekFile)(fileBrowser_file *, unsigned int, unsigned int) = NULL;
int (*romFile_deinit)(fileBrowser_file *) = NULL;

int (*saveFile_init)(fileBrowser_file *) = NULL;
int (*saveFile_readFile)(fileBrowser_file *, void *, unsigned int) = NULL;
int (*saveFile_writeFile)(fileBrowser_file *, void *, unsigned int) = NULL;
int (*saveFile_deinit)(fileBrowser_file *) = NULL;

/* ---- ROM held in memory ---- */

static const unsigned char *rom_data;
static unsigned int rom_size;

static int mem_rom_init(fileBrowser_file *f)
{
	f->offset = 0;
	f->size = rom_size;
	return rom_data ? 0 : FILE_BROWSER_ERROR;
}

static int mem_rom_read(fileBrowser_file *f, void *buffer, unsigned int length)
{
	if (!rom_data || !buffer)
		return FILE_BROWSER_ERROR;
	if (f->offset >= rom_size)
		return 0;
	if (length > rom_size - f->offset)
		length = rom_size - f->offset;
	memcpy(buffer, rom_data + f->offset, length);
	f->offset += length;
	return (int)length;
}

static int mem_rom_seek(fileBrowser_file *f, unsigned int where, unsigned int type)
{
	unsigned int target;

	switch (type) {
	case FILE_BROWSER_SEEK_SET: target = where; break;
	case FILE_BROWSER_SEEK_CUR: target = f->offset + where; break;
	case FILE_BROWSER_SEEK_END: target = rom_size > where ? rom_size - where : 0; break;
	default: return FILE_BROWSER_ERROR;
	}
	if (target > rom_size)
		return FILE_BROWSER_ERROR;
	f->offset = target;
	return 0;
}

static int mem_rom_deinit(fileBrowser_file *f)
{
	(void)f;
	return 0;
}

void wiiu_rom_source_set(const void *data, unsigned int size, fileBrowser_file *out)
{
	rom_data = (const unsigned char *)data;
	rom_size = size;

	romFile_init = mem_rom_init;
	romFile_readFile = mem_rom_read;
	romFile_seekFile = mem_rom_seek;
	romFile_deinit = mem_rom_deinit;

	memset(out, 0, sizeof(*out));
	out->size = size;
	strncpy(out->name, "memory", sizeof(out->name) - 1);
}

void wiiu_rom_source_clear(void)
{
	rom_data = NULL;
	rom_size = 0;
}

/* ---- saves through stdio ---- */

static int stdio_save_init(fileBrowser_file *f)
{
	(void)f;
	return 0;
}

static int stdio_save_read(fileBrowser_file *f, void *buffer, unsigned int length)
{
	FILE *fp = fopen(f->name, "rb");
	size_t got;

	if (!fp)
		return FILE_BROWSER_ERROR;
	if (fseek(fp, (long)f->offset, SEEK_SET) != 0) {
		fclose(fp);
		return FILE_BROWSER_ERROR;
	}
	got = fread(buffer, 1, length, fp);
	fclose(fp);
	f->offset += (unsigned int)got;
	return (int)got;
}

static int stdio_save_write(fileBrowser_file *f, void *buffer, unsigned int length)
{
	/* Write to a temporary file and rename it, so a power loss or a pulled
	 * SD card in the middle of a save cannot destroy the previous save. */
	char tmp[FILE_BROWSER_MAX_PATH_LEN + 8];
	FILE *fp;
	size_t put;

	if (strlen(f->name) + 5 > sizeof(tmp))
		return FILE_BROWSER_ERROR;
	snprintf(tmp, sizeof(tmp), "%s.tmp", f->name);

	fp = fopen(tmp, "wb");
	if (!fp)
		return FILE_BROWSER_ERROR;
	put = fwrite(buffer, 1, length, fp);
	if (fclose(fp) != 0 || put != length) {
		remove(tmp);
		return FILE_BROWSER_ERROR;
	}
	/* FAT cannot replace an existing file with rename() on every driver. */
	remove(f->name);
	if (rename(tmp, f->name) != 0) {
		remove(tmp);
		return FILE_BROWSER_ERROR;
	}
	f->offset += (unsigned int)put;
	return (int)put;
}

void wiiu_save_backend_install(void)
{
	saveFile_init = stdio_save_init;
	saveFile_readFile = stdio_save_read;
	saveFile_writeFile = stdio_save_write;
	saveFile_deinit = stdio_save_init;
}
