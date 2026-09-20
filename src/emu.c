// SPDX-License-Identifier: GPL-2.0-only
/*
 * Bloom!
 *
 * Copyright (C) 2024 Paul Cercueil <paul@crapouillou.net>
 */


#include <kos.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <libpcsxcore/misc.h>
#include <libpcsxcore/plugins.h>
#include <libpcsxcore/psxcommon.h>
#include <libpcsxcore/psxmem.h>
#include <libpcsxcore/r3000a.h>
#include <libpcsxcore/sio.h>
#include <psemu_plugin_defs.h>

#include <arch/gdb.h>
#include <dc/cdrom.h>
#include <dc/video.h>

#include <sys/stat.h>

#include "bloom-config.h"
#include "emu.h"
#include "menu_util.h"
#include "pvr.h"
#include "settings.h"

int fs_fat_init(void);
void fs_fat_shutdown(void);

static bool is_exe;

extern uint32_t _arch_mem_top;

bool started;
unsigned int screen_width = (WITH_480P ? 640 : 320) << WITH_FSAA;
unsigned int screen_height = WITH_480P ? 480 : 240;

void emu_apply_video_settings(void)
{
	int p480 = bloom_settings_video_480p();
	int fsaa = bloom_settings_fsaa();

	screen_width = (unsigned int)((p480 ? 640 : 320) << fsaa);
	screen_height = p480 ? 480 : 240;
}

void SysPrintf(const char *fmt, ...) {
	va_list list;

	va_start(list, fmt);
	vfprintf(stdout, fmt, list);
	va_end(list);
}

void SysMessage(const char *fmt, ...) {
	va_list list;
	char msg[512];
	int ret;

	va_start(list, fmt);
	ret = vsnprintf(msg, sizeof(msg), fmt, list);
	va_end(list);

	if (ret > 0 && (size_t)ret < sizeof(msg) && msg[ret - 1] == '\n')
		msg[ret - 1] = 0;

	SysPrintf("%s\n", msg);
}

static void init_config(void)
{
	memset(&Config, 0, sizeof(Config));

	Config.PsxAuto = 1;
	Config.cycle_multiplier = CYCLE_MULT_DEFAULT;
	Config.GpuListWalking = -1;
	Config.FractionalFramerate = -1;

	strcpy(Config.Mcd1, WITH_MCD1_PATH);
	strcpy(Config.Mcd2, WITH_MCD2_PATH);

	strcpy(Config.PluginsDir, "plugins");
	strcpy(Config.Gpu, "builtin_gpu");
	strcpy(Config.Spu, "builtin_spu");
}

static unsigned int screenshot_num;

static void emu_screenshot(uint8_t port, uint32_t)
{
	maple_device_t *dev;
	cont_state_t *state;
	char buf[1024];

	dev = maple_enum_dev(port, 0);
	state = maple_dev_status(dev);

	if (state->start) {
		snprintf(buf, sizeof(buf), "/pc/screenshot%03u.ppm",
			 ++screenshot_num);
		vid_screen_shot(buf);
	}
}

static void emu_exit(uint8_t, uint32_t)
{
	psxRegs.stop = 1;
}

static int load_boot_sstate(const char *path)
{
	s8 *mapped_psxR = psxR;
	int ret;

	/* LoadState writes the BIOS area, so we can't use the read-only virtual
	 * mapping - temporarily switch to the backing area */
	psxR = (s8 *)(_arch_mem_top + 0x10000);

	ret = LoadState(path);

	psxR = mapped_psxR;

	return ret;
}

static int last_cd_error;

const char *emu_last_cd_error(void)
{
	return menu_cd_error_text(last_cd_error);
}

bool emu_check_cd(const char *path)
{
	int plugins;

	last_cd_error = MENU_CD_OK;
	SetIsoFile(path);

	if (ReloadCdromPlugin() < 0) {
		last_cd_error = MENU_CD_ERR_CDR;
		fprintf(stderr, "%s\n", emu_last_cd_error());
		return false;
	}

	plugins = OpenPlugins();
	if (plugins < 0) {
		last_cd_error = menu_cd_error_from_open(plugins);
		fprintf(stderr, "%s\n", emu_last_cd_error());
		return false;
	}

	is_exe = false;
	if (path) {
		const char *ext = strrchr(path, '.');

		if (ext && !strcasecmp(ext, ".exe"))
			is_exe = true;
	}

	if (!is_exe && CheckCdrom() != 0) {
		ClosePlugins();
		last_cd_error = path ? MENU_CD_ERR_NOT_PSX : MENU_CD_ERR_NO_DISC;
		return false;
	}

	return true;
}

/* Copy of the default params, but FSAA/clip lists follow Settings. */
static void emu_pvr_params(pvr_init_params_t *params)
{
	int clip = HARDWARE_ACCELERATED && bloom_settings_clipping();

	*params = (pvr_init_params_t){
		.opb_sizes = {
			PVR_BINSIZE_16,
			PVR_BINSIZE_0,
			HARDWARE_ACCELERATED ? PVR_BINSIZE_16 : PVR_BINSIZE_0,
			clip ? PVR_BINSIZE_8 : PVR_BINSIZE_0,
			HARDWARE_ACCELERATED ? PVR_BINSIZE_16 : PVR_BINSIZE_0,
		},
		.vertex_buf_size = 768 * 1024,
		.fsaa_enabled = bloom_settings_fsaa(),
		.opb_overflow_count = 3,
	};
}

int main(int argc, char **argv)
{
	enum vid_display_mode_generic video_mode;
	bool should_exit;

	if (WITH_GDB)
		gdb_init();

	if (WITH_IDE || WITH_SDCARD)
		fs_fat_init();

	if (WITH_IDE)
		ide_init();
	if (WITH_SDCARD)
		sdcard_init();

	{
		struct bloom_settings_boot boot = {
			.video_480p = WITH_480P,
			.bilinear = WITH_BILINEAR,
			.silent_audio = strcmp(SPU_PLUGIN, "Null") == 0,
			.hybrid = WITH_HYBRID_RENDERING,
			.clipping = WITH_CLIPPING,
			.fsaa = WITH_FSAA,
			.allow_480p = WITH_480P,
			.allow_aica = strcmp(SPU_PLUGIN, "AICA") == 0,
			.allow_bilinear = HARDWARE_ACCELERATED,
			.allow_hybrid = HARDWARE_ACCELERATED && WITH_HYBRID_RENDERING,
			.allow_clipping = HARDWARE_ACCELERATED && WITH_CLIPPING,
			.allow_fsaa = HARDWARE_ACCELERATED && WITH_FSAA,
		};

		bloom_settings_init(&boot);
	}
	emu_apply_video_settings();

	input_init();

	init_config();

	if (WITH_CACHED_STDIO)
		setvbuf(stdout, NULL, _IOFBF, 0);

	if (EmuInit() == -1) {
		fprintf(stderr, "Could not initialize PCSX core\n");
		return 1;
	}

	if (LoadPlugins() < 0) {
		fprintf(stderr, "Could not load plugins\n");
		return 1;
	}

	plugin_call_rearmed_cbs();

	cont_btn_callback(0, CONT_RESET_BUTTONS, emu_exit);
	cont_btn_callback(0, CONT_START | CONT_DPAD_UP, emu_screenshot);

	do {
		started = false;

		if (WITH_GAME_PATH[0]) {
			if (!emu_check_cd(WITH_GAME_PATH)) {
				fprintf(stderr, "%s\n", emu_last_cd_error());
				return 1;
			}
			ClosePlugins();
		} else {
			vid_set_mode(DM_640x480, PM_RGB888P);
			pvr_init_defaults();

			should_exit = runMenu();
			ClosePlugins();
			pvr_shutdown();

			if (should_exit)
				break;
		}

		emu_apply_video_settings();

		if (bloom_settings_video_480p())
			video_mode = DM_640x480;
		else
			video_mode = DM_320x240;

		if (WITH_24BPP)
			vid_set_mode(video_mode, PM_RGB888P); /* 24-bit */
		else
			vid_set_mode(video_mode, PM_RGB565); /* 16-bit */

		{
			pvr_init_params_t pvr_params;

			emu_pvr_params(&pvr_params);
			/* Re-init PVR without translucent polygon autosort */
			pvr_init(&pvr_params);
		}

		pvr_set_vertical_scale(1.0f);

		PVR_SET(PVR_OBJECT_CLIP, 0.00001f);

		if (HARDWARE_ACCELERATED)
			pvr_renderer_init();

		if (OpenPlugins() < 0) {
			fprintf(stderr, "Could not open plugins\n");
			if (HARDWARE_ACCELERATED)
				pvr_renderer_shutdown();
			pvr_shutdown();
			if (WITH_GAME_PATH[0])
				return 1;
			continue;
		}

		started = true;
		EmuReset();

		if (UsingIso() && !!strncmp(GetIsoFile(), "/cd", sizeof("/cd") - 1))
			cdrom_spin_down();

		if (is_exe)
			Load(GetIsoFile());
		else
			LoadCdrom();

		mcd_fs_init();

		if (WITH_BOOT_SSTATE[0] && load_boot_sstate(WITH_BOOT_SSTATE) == 0)
			printf("Loaded savestate %s\n", WITH_BOOT_SSTATE);

		psxRegs.stop = 0;

		while (!psxRegs.stop)
			psxCpu->Execute(&psxRegs);

		ClosePlugins();

		if (HARDWARE_ACCELERATED)
			pvr_renderer_shutdown();

		pvr_shutdown();
		mcd_fs_shutdown();
	} while (!WITH_GAME_PATH[0]);

	printf("Exit...\n");
	EmuShutdown();
	ReleasePlugins();

	input_shutdown();

	if (WITH_SDCARD)
		sdcard_shutdown();
	if (WITH_IDE)
		ide_shutdown();
	if (WITH_IDE || WITH_SDCARD)
		fs_fat_shutdown();

	return 0;
}

mode_t umask(mode_t mask) {
	return mask;
}

int chmod(const char *pathname, mode_t mode)
{
	return 0;
}

void lightrec_code_inv(void *ptr, uint32_t len)
{
	icache_sync_range((uintptr_t)ptr, len);
}

static void copy_bios(void)
{
	if (WITH_EMBEDDED_BIOS_PATH)
		memcpy((uint8_t *)(_arch_mem_top + 0x10000), _bss_start, 0x80000);
}
KOS_INIT_EARLY(copy_bios);

void psxMemReset()
{
	bool success = false;
	file_t fd;

	if (WITH_BIOS_PATH[0]) {
		fd = fs_open(WITH_BIOS_PATH, O_RDONLY);

		if (fd != -1) {
			success = load_bios(fd);
			fs_close(fd);
		}
	}

	Config.HLE = !success && !WITH_EMBEDDED_BIOS_PATH;
	Config.SlowBoot = 1;
}
