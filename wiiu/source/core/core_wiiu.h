#ifndef WIIU_CORE_H
#define WIIU_CORE_H

/* What the Wii U app needs from the N64 emulator core. This is the only header
 * the C++ side includes, so none of the core's old headers leak into it. */

#ifdef __cplusplus
extern "C" {
#endif

enum {
	N64_OK = 0,
	N64_ERR_READ = -1,
	N64_ERR_INVALID_ROM = -2,
	N64_ERR_MEMORY = -3,
	N64_ERR_NOT_LOADED = -4
};

/* What sits in a controller's Pak slot (same values as PLUGIN_* in main/plugin.h). */
enum {
	N64_PAK_NONE = 1,
	N64_PAK_MEMORY = 2,
	N64_PAK_RUMBLE = 3
};

/* CPU cores, same order as the core's dynacore variable. */
enum {
	N64_CORE_CACHED_INTERPRETER = 0,
	N64_CORE_DYNAREC = 1,
	N64_CORE_PURE_INTERPRETER = 2
};

typedef struct {
	char name[24];        /* header name, trailing spaces removed, safe for file names */
	char region[8];       /* "(U)", "(E)", "(J)" ... as used in save file names */
	char country_code;
	unsigned int size;    /* in bytes */
	unsigned int crc1;
	unsigned int crc2;
	int eeprom_16k;       /* 1 when the game uses the 16 kbit EEPROM */
} N64RomInfo;

/* Allocates the core's run-time tables. Call once at start-up. */
int  n64_core_init(void);
void n64_core_shutdown(void);

/* Loads a ROM image (z64, v64 or n64 byte order). `data` is read during the
 * call and is not kept. */
int  n64_load_rom(const void *data, unsigned int size, N64RomInfo *info);
void n64_unload_rom(void);
int  n64_rom_loaded(void);

/* Runs the emulated machine on the calling thread until n64_request_stop(). */
void n64_run(void);
void n64_request_stop(void);
int  n64_is_stopped(void);

/* Power-cycles the loaded game (saves in memory are kept). */
int  n64_reset(void);

/* Which CPU core to use, and the 1..3 cycles-per-instruction timing guess. */
void n64_set_cpu_core(int core);
int  n64_get_cpu_core(void);
void n64_set_count_per_op(int count);

/* Game audio on or off, and whether to hold the game to the console's frame rate. */
void n64_set_audio_enabled(int on);
void n64_set_frame_limit(int on);

/* 1 when the code recompiler can run on this system (executable memory). */
int  n64_dynarec_available(void);

/* Native game saves (EEPROM, SRAM, FlashRAM, Controller Pak) in `dir`.
 * Both return how many kinds of save were loaded / written, or < 0 on error. */
int  n64_saves_load(const char *dir);
int  n64_saves_save(const char *dir);
int  n64_saves_dirty(void);

/* Save states. The folder is shared by all games, files are named after the ROM. */
void n64_state_set_folder(const char *dir);
int  n64_state_exists(int slot);
int  n64_state_save(int slot);
int  n64_state_load(int slot);

/* Pace / frame statistics */
float n64_get_vi_rate(void);

/* The app registers this to be called once per emulated frame, on the emulation
 * thread. It polls input and decides whether to keep running. */
void n64_set_frame_callback(void (*cb)(void));

#ifdef __cplusplus
}
#endif

#endif
