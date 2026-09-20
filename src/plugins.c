// SPDX-License-Identifier: GPL-2.0-only
/*
 * Open/Close plugins implementation
 *
 * Copyright (C) 2024 Paul Cercueil <paul@crapouillou.net>
 */

#include <libpcsxcore/cdrom-async.h>
#include <libpcsxcore/plugins.h>

#include "bloom-config.h"
#include "emu.h"
#include "menu_util.h"

void SPUirq(int);

static unsigned long gpuDisp;
static int plugins_opened;

static int _OpenPlugins() {
	int ret;

	cdra_set_buf_count(WITH_CDROM_CACHE_SIZE);

	ret = cdra_open();
	if (ret < 0) {
		SysPrintf("Error Opening CDR Plugin\n");
		return -MENU_CD_ERR_CDR;
	}
	ret = SPU_open();
	if (ret < 0) {
		SysPrintf("Error Opening SPU Plugin\n");
		cdra_close();
		return -MENU_CD_ERR_SPU;
	}
	SPU_registerCallback(SPUirq);
	SPU_registerScheduleCb(SPUschedule);
	ret = GPU_open(&gpuDisp, "PCSX", NULL);
	if (ret < 0) {
		SysPrintf("Error Opening GPU Plugin\n");
		SPU_close();
		cdra_close();
		return -MENU_CD_ERR_GPU;
	}

	return 0;
}

int OpenPlugins() {
	int ret;

	plugin_call_rearmed_cbs();
	if (plugins_opened)
		return 0;
	ret = _OpenPlugins();
	if (ret == 0)
		plugins_opened = 1;
	return ret;
}

void ClosePlugins() {
	int ret;

	if (!plugins_opened)
		return;
	plugins_opened = 0;

	cdra_close();
	ret = SPU_close();
	if (ret < 0)
		SysPrintf("Error Closing SPU Plugin\n");
	ret = GPU_close();
	if (ret < 0)
		SysPrintf("Error Closing GPU Plugin\n");
}

void ResetPlugins() {
	int ret;

	plugins_opened = 0;
	cdra_shutdown();
	GPU_shutdown();
	SPU_shutdown();

	ret = cdra_init();
	if (ret < 0) { SysPrintf("CDRinit error: %d\n", ret); return; }
	ret = GPU_init();
	if (ret < 0) { SysPrintf("GPUinit error: %d\n", ret); return; }
	ret = SPU_init();
	if (ret < 0) { SysPrintf("SPUinit error: %d\n", ret); return; }
}
