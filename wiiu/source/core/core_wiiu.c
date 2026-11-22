/* Facade over the not64 emulator core for the Wii U app.
 *
 * Replaces what main/main_gc-menu2.cpp did on the Wii: owns the plugin info
 * structures, loads and resets games, drives the emulation loop and forwards
 * the per-frame hook to the app. Everything the C++ side may call is declared
 * in core_wiiu.h.
 */
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../main/winlnxdefs.h"
#include "../../../main/main.h"
#include "../../../main/rom.h"
#include "../../../main/plugin.h"
#include "../../../main/timers.h"
#include "../../../main/wii64config.h"
#include "../../../main/savestates.h"
#include "../../../main/ROM-Cache.h"
#include "../../../gc_input/controller.h"
#include "../../../r4300/interupt.h"
#include "../../../r4300/r4300.h"
#include "../../../gc_memory/memory.h"
#include "../../../gc_memory/tlb.h"
#include "../../../gc_memory/pif.h"
#include "../../../gc_memory/flashram.h"
#include "../../../gc_memory/Saves.h"

#include "core_wiiu.h"
#include "wiiu_mem.h"
#include "save_wiiu.h"

/* ---- configuration the core reads (set by the app through Timers etc.) ---- */

CONTROL Controls[4];
AUDIO_INFO audio_info;

static GFX_INFO gfx_info;
static CONTROL_INFO control_info;
static RSP_INFO rsp_info;

char menuActive;
char skipMenu;
char creditsScrolling;
char shutdown = 0;
char nativeSaveDevice;
char saveStateDevice;
char autoLoadSave;
char autoSave;
char screenMode = 0;
char videoFormat;
char videoMode = 0;
char videoWidth;
char pixelClock;
char trapFilter;
char pollRate;
char padAutoAssign;
char padType[4];
char padAssign[4];
char pakMode[4];
char loadButtonSlot;
char showFPSonScreen;
char printToScreen;
char printToSD;
char glN64_useFrameBufferTextures;
char glN64_use2xSaiTextures;
char renderCpuFramebuffer;

BOOL hasLoadedROM = FALSE;
int autoLoadedSave = NATIVESAVEDEVICE_NONE;

extern BOOL eepromWritten;
extern BOOL mempakWritten;
extern BOOL sramWritten;
extern BOOL flashramWritten;

static void (*frame_callback)(void);
static char state_folder[256];
static int core_ready;

/* ---- plugin info ---- */

static void gfx_info_init(void)
{
	gfx_info.MemoryBswaped = TRUE;
	gfx_info.HEADER = (BYTE *)&ROM_HEADER;
	gfx_info.RDRAM = (BYTE *)rdram;
	gfx_info.DMEM = (BYTE *)SP_DMEM;
	gfx_info.IMEM = (BYTE *)SP_IMEM;
	gfx_info.MI_INTR_REG = &(MI_register.mi_intr_reg);
	gfx_info.DPC_START_REG = &(dpc_register.dpc_start);
	gfx_info.DPC_END_REG = &(dpc_register.dpc_end);
	gfx_info.DPC_CURRENT_REG = &(dpc_register.dpc_current);
	gfx_info.DPC_STATUS_REG = &(dpc_register.dpc_status);
	gfx_info.DPC_CLOCK_REG = &(dpc_register.dpc_clock);
	gfx_info.DPC_BUFBUSY_REG = &(dpc_register.dpc_bufbusy);
	gfx_info.DPC_PIPEBUSY_REG = &(dpc_register.dpc_pipebusy);
	gfx_info.DPC_TMEM_REG = &(dpc_register.dpc_tmem);
	gfx_info.VI_STATUS_REG = &(vi_register.vi_status);
	gfx_info.VI_ORIGIN_REG = &(vi_register.vi_origin);
	gfx_info.VI_WIDTH_REG = &(vi_register.vi_width);
	gfx_info.VI_INTR_REG = &(vi_register.vi_v_intr);
	gfx_info.VI_V_CURRENT_LINE_REG = &(vi_register.vi_current);
	gfx_info.VI_TIMING_REG = &(vi_register.vi_burst);
	gfx_info.VI_V_SYNC_REG = &(vi_register.vi_v_sync);
	gfx_info.VI_H_SYNC_REG = &(vi_register.vi_h_sync);
	gfx_info.VI_LEAP_REG = &(vi_register.vi_leap);
	gfx_info.VI_H_START_REG = &(vi_register.vi_h_start);
	gfx_info.VI_V_START_REG = &(vi_register.vi_v_start);
	gfx_info.VI_V_BURST_REG = &(vi_register.vi_v_burst);
	gfx_info.VI_X_SCALE_REG = &(vi_register.vi_x_scale);
	gfx_info.VI_Y_SCALE_REG = &(vi_register.vi_y_scale);
	gfx_info.CheckInterrupts = check_interupt;
	initiateGFX(gfx_info);
}

static void audio_info_init(void)
{
	audio_info.MemoryBswaped = TRUE;
	audio_info.HEADER = (BYTE *)&ROM_HEADER;
	audio_info.RDRAM = (BYTE *)rdram;
	audio_info.DMEM = (BYTE *)SP_DMEM;
	audio_info.IMEM = (BYTE *)SP_IMEM;
	audio_info.MI_INTR_REG = &(MI_register.mi_intr_reg);
	audio_info.AI_DRAM_ADDR_REG = &(ai_register.ai_dram_addr);
	audio_info.AI_LEN_REG = &(ai_register.ai_len);
	audio_info.AI_CONTROL_REG = &(ai_register.ai_control);
	audio_info.AI_STATUS_REG = &(ai_register.ai_status);
	audio_info.AI_DACRATE_REG = &(ai_register.ai_dacrate);
	audio_info.AI_BITRATE_REG = &(ai_register.ai_bitrate);
	audio_info.CheckInterrupts = check_interupt;
	initiateAudio(audio_info);
}

void control_info_init(void)
{
	int i;

	control_info.MemoryBswaped = TRUE;
	control_info.HEADER = (BYTE *)&ROM_HEADER;
	control_info.Controls = Controls;
	for (i = 0; i < 4; i++) {
		Controls[i].Present = FALSE;
		Controls[i].RawData = FALSE;
		Controls[i].Plugin = PLUGIN_NONE;
	}
	initiateControllers(control_info);
}

static void rsp_info_init(void)
{
	static int cycle_count;

	rsp_info.MemoryBswaped = TRUE;
	rsp_info.RDRAM = (BYTE *)rdram;
	rsp_info.DMEM = (BYTE *)SP_DMEM;
	rsp_info.IMEM = (BYTE *)SP_IMEM;
	rsp_info.MI_INTR_REG = &MI_register.mi_intr_reg;
	rsp_info.SP_MEM_ADDR_REG = &sp_register.sp_mem_addr_reg;
	rsp_info.SP_DRAM_ADDR_REG = &sp_register.sp_dram_addr_reg;
	rsp_info.SP_RD_LEN_REG = &sp_register.sp_rd_len_reg;
	rsp_info.SP_WR_LEN_REG = &sp_register.sp_wr_len_reg;
	rsp_info.SP_STATUS_REG = &sp_register.sp_status_reg;
	rsp_info.SP_DMA_FULL_REG = &sp_register.sp_dma_full_reg;
	rsp_info.SP_DMA_BUSY_REG = &sp_register.sp_dma_busy_reg;
	rsp_info.SP_PC_REG = &rsp_register.rsp_pc;
	rsp_info.SP_SEMAPHORE_REG = &sp_register.sp_semaphore_reg;
	rsp_info.DPC_START_REG = &dpc_register.dpc_start;
	rsp_info.DPC_END_REG = &dpc_register.dpc_end;
	rsp_info.DPC_CURRENT_REG = &dpc_register.dpc_current;
	rsp_info.DPC_STATUS_REG = &dpc_register.dpc_status;
	rsp_info.DPC_CLOCK_REG = &dpc_register.dpc_clock;
	rsp_info.DPC_BUFBUSY_REG = &dpc_register.dpc_bufbusy;
	rsp_info.DPC_PIPEBUSY_REG = &dpc_register.dpc_pipebusy;
	rsp_info.DPC_TMEM_REG = &dpc_register.dpc_tmem;
	rsp_info.CheckInterrupts = check_interupt;
	rsp_info.ProcessDlistList = processDList;
	rsp_info.ProcessAlistList = processAList;
	rsp_info.ProcessRdpList = processRDPList;
	rsp_info.ShowCFB = showCFB;
	initiateRSP(rsp_info, (DWORD *)&cycle_count);
}

/* ---- hooks the core expects the platform to provide ---- */

void pauseRemovalThread(void) {}
void continueRemovalThread(void) {}

void wiiu_vi_hook(void)
{
	if (frame_callback)
		frame_callback();
}

void wiiu_state_filename(char *out, unsigned size, unsigned slot)
{
	snprintf(out, size, "%s/%s%s.st%u", state_folder, ROM_SETTINGS.goodname,
	         saveregionstr(), slot);
}

/* ---- public interface ---- */

int n64_core_init(void)
{
	if (core_ready)
		return N64_OK;

	if (wiiu_mem2_init() != 0)
		return N64_ERR_MEMORY;

	wiiu_save_backend_install();

	/* defaults the core and the plugins read */
	audioEnabled = AUDIO_ENABLE;
	scalePitch = SCALEPITCH_ENABLE;
	showFPSonScreen = FPS_HIDE;
	printToScreen = DEBUG_HIDE;
	printToSD = SDLOG_DISABLE;
	Timers.limitVIs = LIMITVIS_WAIT_FOR_VI;
	autoLoadSave = AUTOLOADSAVE_ENABLE;
	autoSave = AUTOSAVE_ENABLE;
	dynacore = DYNACORE_PURE_INTERP;
	count_per_op = COUNT_PER_OP_1;

	control_info_init();
	core_ready = 1;
	return N64_OK;
}

void n64_core_shutdown(void)
{
	if (!core_ready)
		return;
	n64_unload_rom();
	wiiu_mem2_free();
	core_ready = 0;
}

static void close_plugins_and_cpu(void)
{
	cpu_deinit();
	eepromWritten = FALSE;
	mempakWritten = FALSE;
	sramWritten = FALSE;
	flashramWritten = FALSE;
	romClosed_RSP();
	romClosed_input();
	romClosed_audio();
	romClosed_gfx();
	closeDLL_RSP();
	closeDLL_input();
	closeDLL_audio();
	closeDLL_gfx();
}

static void open_plugins_and_cpu(void)
{
	gfx_info_init();
	audio_info_init();
	rsp_info_init();

	romOpen_gfx();
	romOpen_audio();
	romOpen_input();

	cpu_init();
}

int n64_load_rom(const void *data, unsigned int size, N64RomInfo *info)
{
	fileBrowser_file romFile;
	int ret;

	if (!core_ready)
		return N64_ERR_NOT_LOADED;
	if (!data || size < 0x1000)
		return N64_ERR_INVALID_ROM;

	savestates_job = 0;
	if (hasLoadedROM) {
		close_plugins_and_cpu();
		ROMCache_deinit();
		free_memory();
		hasLoadedROM = FALSE;
	}

	wiiu_mem2_reset();
	format_mempacks();
	reset_flashram();
	init_eeprom();
	tlb_mem2_init();

	wiiu_rom_source_set(data, size, &romFile);
	ret = rom_read(&romFile);
	wiiu_rom_source_clear();
	if (ret) {
		return ret == ROM_CACHE_INVALID_ROM ? N64_ERR_INVALID_ROM : N64_ERR_READ;
	}

	hasLoadedROM = TRUE;
	init_memory();
	open_plugins_and_cpu();

	if (info) {
		memset(info, 0, sizeof(*info));
		memcpy(info->name, ROM_SETTINGS.goodname, sizeof(info->name) - 1);
		strncpy(info->region, saveregionstr(), sizeof(info->region) - 1);
		info->country_code = (char)(ROM_HEADER.Country_code & 0xFF);
		info->size = size;
		info->crc1 = ROM_HEADER.CRC1;
		info->crc2 = ROM_HEADER.CRC2;
		info->eeprom_16k = ROM_SETTINGS.isEEPROM16k ? 1 : 0;
	}
	return N64_OK;
}

void n64_unload_rom(void)
{
	if (!hasLoadedROM)
		return;
	close_plugins_and_cpu();
	ROMCache_deinit();
	free_memory();
	hasLoadedROM = FALSE;
}

int n64_rom_loaded(void)
{
	return hasLoadedROM ? 1 : 0;
}

void n64_run(void)
{
	if (!hasLoadedROM)
		return;
	go();
}

void n64_request_stop(void)
{
	stop = 1;
}

int n64_is_stopped(void)
{
	return stop ? 1 : 0;
}

int n64_reset(void)
{
	if (!hasLoadedROM)
		return N64_ERR_NOT_LOADED;

	/* The ROM stays in the cache; everything else starts over. */
	savestates_job = 0;
	close_plugins_and_cpu();
	free_memory();
	wiiu_mem2_reset();
	tlb_mem2_init();
	init_memory();
	open_plugins_and_cpu();
	return N64_OK;
}

void n64_set_cpu_core(int core)
{
	if (core == N64_CORE_DYNAREC && !n64_dynarec_available())
		core = N64_CORE_PURE_INTERPRETER;
	dynacore = (unsigned long)core;
}

int n64_get_cpu_core(void)
{
	return (int)dynacore;
}

void n64_set_count_per_op(int count)
{
	if (count < COUNT_PER_OP_1 || count > COUNT_PER_OP_3)
		count = COUNT_PER_OP_1;
	count_per_op = (unsigned long)count;
}

void n64_set_audio_enabled(int on)
{
	audioEnabled = on ? AUDIO_ENABLE : AUDIO_DISABLE;
}

void n64_set_frame_limit(int on)
{
	Timers.limitVIs = on ? LIMITVIS_WAIT_FOR_VI : LIMITVIS_NONE;
}

float n64_get_vi_rate(void)
{
	return Timers.vis;
}

void n64_set_frame_callback(void (*cb)(void))
{
	frame_callback = cb;
}

/* ---- saves ---- */

static int with_save_dir(const char *dir, int (*loader)(fileBrowser_file *), fileBrowser_file *f)
{
	memset(f, 0, sizeof(*f));
	strncpy(f->name, dir, sizeof(f->name) - 1);
	return loader(f);
}

int n64_saves_load(const char *dir)
{
	fileBrowser_file f;
	int found = 0, r;

	if (!hasLoadedROM || !dir)
		return N64_ERR_NOT_LOADED;

	if ((r = with_save_dir(dir, loadEeprom, &f)) > 0) found += r;
	if ((r = with_save_dir(dir, loadSram, &f)) > 0) found += r;
	if ((r = with_save_dir(dir, loadMempak, &f)) > 0) found += r;
	if ((r = with_save_dir(dir, loadFlashram, &f)) > 0) found += r;
	return found;
}

int n64_saves_save(const char *dir)
{
	fileBrowser_file f;
	int written = 0, r;

	if (!hasLoadedROM || !dir)
		return N64_ERR_NOT_LOADED;

	if ((r = with_save_dir(dir, saveEeprom, &f)) < 0) return r;
	written += r;
	if ((r = with_save_dir(dir, saveSram, &f)) < 0) return r;
	written += r;
	if ((r = with_save_dir(dir, saveMempak, &f)) < 0) return r;
	written += r;
	if ((r = with_save_dir(dir, saveFlashram, &f)) < 0) return r;
	written += r;
	return written;
}

int n64_saves_dirty(void)
{
	return (eepromWritten || mempakWritten || sramWritten || flashramWritten) ? 1 : 0;
}

/* ---- save states ---- */

void n64_state_set_folder(const char *dir)
{
	strncpy(state_folder, dir ? dir : "", sizeof(state_folder) - 1);
	state_folder[sizeof(state_folder) - 1] = 0;
}

int n64_state_exists(int slot)
{
	if (!hasLoadedROM || slot < 0 || slot > 9)
		return 0;
	savestates_select_slot((unsigned)slot);
	return savestates_exists(LOADSTATE);
}

int n64_state_save(int slot)
{
	if (!hasLoadedROM || slot < 0 || slot > 9)
		return N64_ERR_NOT_LOADED;
	savestates_select_slot((unsigned)slot);
	if (stop)
		savestates_save();          /* not running: do it right now */
	else
		savestates_job |= SAVESTATE; /* running: done at the next safe point */
	return N64_OK;
}

int n64_state_load(int slot)
{
	if (!hasLoadedROM || slot < 0 || slot > 9)
		return N64_ERR_NOT_LOADED;
	savestates_select_slot((unsigned)slot);
	if (stop)
		savestates_load();
	else
		savestates_job |= LOADSTATE;
	return N64_OK;
}

/* ---- recompiler availability ---- */

/* Filled in by the dynarec glue (see wiiu_jit.c) once executable memory is known. */
int wiiu_jit_available(void);

int n64_dynarec_available(void)
{
	return wiiu_jit_available();
}
