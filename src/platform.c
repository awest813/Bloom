// SPDX-License-Identifier: GPL-2.0-only
/*
 * Misc. glue code for the PCSX port
 *
 * Copyright (C) 2024 Paul Cercueil <paul@crapouillou.net>
 */

#include <frontend/plugin_lib.h>
#include <libpcsxcore/psxcounters.h>
#include <libpcsxcore/gpu.h>

#include <kos/thread.h>
#include <arch/timer.h>
#include <dc/matrix.h>
#include <dc/pvr.h>
#include <dc/sq.h>
#include <dc/video.h>
#include <dc/vmu_fb.h>

#include <stdint.h>
#include <string.h>
#include <sys/time.h>

#include "bloom-config.h"
#include "emu.h"
#include "pvr.h"

#define MAX_LAG_FRAMES 3
#define USE_PVR_RENDERER (HARDWARE_ACCELERATED && !WITH_PVR_SOFTWARE)

#define tvdiff(tv, tv_old) \
	((tv.tv_sec - tv_old.tv_sec) * 1000000 + tv.tv_usec - tv_old.tv_usec)

/* PVR texture size in pixels */
#define TEX_WIDTH  1024
#define TEX_HEIGHT 512

static unsigned int frames;
static uint64_t timer_ms;
static bool stats_started;

static pvr_ptr_t pvram;
static uint32_t *pvram_sq;
/* KOS splits this into two 512-byte command buffers. Each scanout needs
 * a header, four vertices, and an end marker (192 bytes). DMA keeps these
 * commands separate from the store queues used to upload display pixels. */
static uint8_t scanout_commands[1024] __attribute__((aligned(32)));

static bool frame_was_24bpp;

float screen_fw, screen_fh;
static unsigned int screen_w, screen_h;
unsigned int screen_bpp;

static uint64_t last_cputime;
static uint64_t last_idletime;

#if WITH_PERF_LOG
/* Measure presentation separately from PS1 execution and software drawing.
 * Reset per output session; averages include only non-blank scanouts. */
static uint64_t scanout_wait_us, scanout_copy_us, scanout_submit_us;
static unsigned int scanout_samples;
static uint64_t scanout_report_ms;

static void scanout_report(uint64_t begin, uint64_t ready, uint64_t copied,
			   int offset, int x, int y, int w, int h)
{
	uint64_t now = timer_ms_gettime64();
	scanout_wait_us += ready - begin;
	scanout_copy_us += copied - ready;
	scanout_submit_us += timer_us_gettime64() - copied;
	scanout_samples++;
	if (now - scanout_report_ms < 5000)
		return;
	printf("PERF-SCANOUT: wait=%.3f copy=%.3f submit=%.3f ms n=%u "
	       "src=%x dst=%d,%d %dx%d tex=%p clip=%08lx\n",
	       (double)scanout_wait_us / (1000 * scanout_samples),
	       (double)scanout_copy_us / (1000 * scanout_samples),
	       (double)scanout_submit_us / (1000 * scanout_samples),
	       scanout_samples, offset, x, y, w, h, pvram,
	       (unsigned long)PVR_GET(PVR_PCLIP_Y));
	scanout_wait_us = scanout_copy_us = scanout_submit_us = 0;
	scanout_samples = 0;
	scanout_report_ms = now;
}
#endif

static void dc_alloc_pvram(void)
{
	pvram = pvr_mem_malloc(TEX_WIDTH * TEX_HEIGHT * 2);

	assert(!!pvram);
	assert(!((unsigned int)pvram & 0x1f));

	pvram_sq = (uint32_t *)(((uintptr_t)pvram & 0xffffff) | PVR_TA_TEX_MEM);
}

static int dc_vout_open(void)
{
	if (!started)
		return 0;

	frame_was_24bpp = false;
	frames = 0;
	stats_started = false;
#if WITH_PERF_LOG
	scanout_wait_us = scanout_copy_us = scanout_submit_us = 0;
	scanout_samples = 0;
	scanout_report_ms = timer_ms_gettime64();
#endif

	if (USE_PVR_RENDERER)
		hw_render_start();
	else {
		dc_alloc_pvram();
		pvr_set_vertbuf(PVR_LIST_OP_POLY, scanout_commands, sizeof(scanout_commands));
	}

	return 0;
}

static void dc_vout_close(void)
{
	if (!started)
		return;

	if (USE_PVR_RENDERER && !frame_was_24bpp)
		hw_render_stop();

	/* Readiness permits a new scene while the previous one still renders.
	 * Finish both stages before releasing any texture storage. */
	pvr_wait_ready();
	pvr_wait_render_done();
	if (!USE_PVR_RENDERER || frame_was_24bpp)
		pvr_mem_free(pvram);
}

static void dc_vout_set_mode(int w, int h, int raw_w, int raw_h, int bpp)
{
	if (!started)
		return;

	screen_w = raw_w;
	screen_h = raw_h;
	screen_bpp = bpp;

	/* Use 1280x480 when using FSAA */
	screen_fw = (float)SCREEN_WIDTH / (float)raw_w;
	screen_fh = (float)SCREEN_HEIGHT / (float)raw_h;

	if (USE_PVR_RENDERER) {
		matrix_t matrix = {
			{ screen_fw, 0.0f, 0.0f, 0.0f },
			{ 0.0f, screen_fh, 0.0f, 0.0f },
			{ 0.0f, 0.0f, 1.0f / 256.0f, 0.0f },
			{ 0.0f, 0.0f, 0.0f, 1.0f / 1024.0f },
		};

		mat_load(&matrix);
	}
}

static inline uint16_t rgb_24_to_16(uint8_t r, uint8_t g, uint8_t b)
{
	return ((uint16_t)r & 0xf8) << 8
		| ((uint16_t)g & 0xfc) << 3
		| (uint16_t)b >> 3;
}

/* Keep the VRAM base so shifted/cropped scanout can be bounded independently
 * of store-queue padding. Source rows retain gpulib's 2048-byte stride. */
/* Keep the hot integer loop separate from the presentation callback's float
 * vertices and timing state to avoid SH-4 register spills. */
static void copy_scanout(const void *vram, int offset, int w, int h, bool bgr24)
	__attribute__((noinline));

static void copy_scanout(const void *vram, int offset, int w, int h, bool bgr24)
{
	const uint8_t *bytes = vram;
	const unsigned int vram_bytes = TEX_WIDTH * TEX_HEIGHT * 2;
	const unsigned int pixel_bytes = bgr24 ? 3 : 2;
	uint32_t *dest, *line;
	unsigned int x, y, i, available, row_offset;
	uint16_t pixel[16];

	if (!vram || offset < 0 || (unsigned int)offset >= vram_bytes
	    || w <= 0 || w > TEX_WIDTH || h <= 0 || h > TEX_HEIGHT)
		return;

	/* KOS maps two consecutive 1 MiB SQ pages. The complete texture is
	 * only 1 MiB, so keep that mapping stable across every row, including
	 * a texture spanning a physical page boundary. Re-locking per row
	 * needlessly rewrites the TLB and takes a mutex hundreds of times. */
	dest = sq_lock(pvram_sq);
	for (y = 0; y < (unsigned int)h; y++) {
		row_offset = (unsigned int)offset + y * TEX_WIDTH * 2;
		available = row_offset < vram_bytes ? vram_bytes - row_offset : 0;
		line = dest;

		for (x = 0; x < (unsigned int)w; x += 16) {
			unsigned int pos = x * pixel_bytes;
			/* Retain word-at-a-time conversion for complete aligned blocks.
			 * memcpy avoids aliasing a uint16_t VRAM allocation as uint32_t. */
			if (x + 16 <= (unsigned int)w && pos + 16 * pixel_bytes <= available
			    && !((uintptr_t)(bytes + row_offset + pos) & 3)) {
				const uint8_t *src = __builtin_assume_aligned(bytes + row_offset + pos, 4);
				if (bgr24) {
					for (i = 0; i < 8; i += 2) {
						uint32_t a, b, c;
						memcpy(&a, src, 4);
						memcpy(&b, src + 4, 4);
						memcpy(&c, src + 8, 4);
						src += 12;
						line[i] = rgb_24_to_16(a, a >> 8, a >> 16)
							| ((uint32_t)rgb_24_to_16(a >> 24, b, b >> 8) << 16);
						line[i + 1] = rgb_24_to_16(b >> 16, b >> 24, c)
							| ((uint32_t)rgb_24_to_16(c >> 8, c >> 16, c >> 24) << 16);
					}
				} else {
					/* One complete store queue: expose independent pixel
					 * pairs so SH-4 can schedule loads and bit operations
					 * without a branch for every two pixels. */
#pragma GCC unroll 8
					for (i = 0; i < 8; i++) {
						uint32_t value;
						memcpy(&value, src + i * 4, 4);
						line[i] = ((value >> 10) & 0x001f001f)
							| (value & 0x03e003e0) | ((value & 0x001f001f) << 10);
					}
				}
				sq_flush(line);
				line += 8;
				continue;
			}
			/* Convert only logical pixels. An incomplete source pixel or
			 * a padded destination pixel is black, never an extra read. */
			for (i = 0; i < 16; i++) {
				unsigned int pos = (x + i) * pixel_bytes;
				pixel[i] = 0;
				if (x + i >= (unsigned int)w || pos + pixel_bytes > available)
					continue;
				const uint8_t *src = bytes + row_offset + pos;
				if (bgr24) {
					pixel[i] = rgb_24_to_16(src[0], src[1], src[2]);
				} else {
					uint16_t value = src[0] | ((uint16_t)src[1] << 8);
					pixel[i] = ((value & 31) << 10) | (value & 0x3e0)
						| ((value >> 10) & 31);
				}
			}
			for (i = 0; i < 8; i++)
				line[i] = pixel[i * 2] | ((uint32_t)pixel[i * 2 + 1] << 16);
			sq_flush(line);
			line += 8;
		}
		dest += TEX_WIDTH / 2;
	}
	sq_unlock();
}

/* Sample only completed, non-blank presentation callbacks. */
static void dc_vout_report_stats(void)
{
	float idle_diff, cpu_diff, fps, busy;
	uint64_t new_timer, cputime, idletime;
	pvr_stats_t pvr_stats;

	new_timer = timer_ms_gettime64();

	frames++;

	if (!stats_started) {
		stats_started = true;
		timer_ms = new_timer;
		last_cputime = new_timer;
		last_idletime = thd_get_cpu_time(thd_get_idle());
		frames = 0;
		return;
	}

	if (new_timer - timer_ms >= 1000) {
		pvr_get_stats(&pvr_stats);

		cputime = timer_ms_gettime64();
		idletime = thd_get_cpu_time(thd_get_idle());

		idle_diff = idletime - last_idletime;
		cpu_diff = cputime - last_cputime;
		fps = (float)frames * 1000.0f / (float)(new_timer - timer_ms);
		busy = 100.0f - 100.0f * idle_diff / cpu_diff;
		if (busy < 0.0f)
			busy = 0.0f;
		if (busy > 100.0f)
			busy = 100.0f;

		vmu_printf(" FPS: %5.1f\n\n %ux%u-%u\n PVR %02.02f%%\n SH4 %02.02f%%",
			   fps, screen_w, screen_h, screen_bpp,
			   (float)pvr_stats.rnd_last_time * 100.0f / 16666666.7f,
			   busy);

		if (WITH_PERF_LOG)
			printf("PERF: flip=%.2f fps SH4=%.1f%% PVR-last=%.3f ms %ux%u-%u\n",
			       fps, busy, (float)pvr_stats.rnd_last_time / 1000000.0f,
			       screen_w, screen_h, screen_bpp);
#if WITH_PERF_LOG && HARDWARE_ACCELERATED
		pvr_perf_report();
#endif

		timer_ms = new_timer;
		frames = 0;

		last_cputime = cputime;
		last_idletime = idletime;
	}
}

static void dc_vout_flip(const void *vram, int offset, int bgr24,
			 int x, int y, int w, int h, int dims_changed)
{
	float ymin, ymax, xmin, xmax;
	pvr_poly_cxt_t cxt;
	pvr_poly_hdr_t hdr;
	pvr_vertex_t vert;

	if (!started)
		return;

	if (!vram) {
		/* gpulib uses NULL for display disable. Close the pending scene
		 * and show black instead of leaving its queues open indefinitely. */
		if (USE_PVR_RENDERER && !frame_was_24bpp) {
			hw_render_stop();
			hw_render_start();
		} else {
			pvr_wait_ready();
			pvr_scene_begin();
			pvr_scene_finish();
		}
		return;
	}

	if (USE_PVR_RENDERER && !frame_was_24bpp) {
		/* Render the old frame */
		hw_render_stop();

		if (bgr24) {
			invalidate_all_textures();
			dc_alloc_pvram();
		}
	}

	if (USE_PVR_RENDERER && !bgr24) {
		if (frame_was_24bpp) {
			pvr_wait_ready();
			pvr_wait_render_done();
			pvr_mem_free(pvram);
		}

		/* Prepare the next frame */
		hw_render_start();
	} else {
		/* This presentation texture is reused each frame. TA readiness
		 * alone does not mean the rasterizer has finished sampling it. */
#if WITH_PERF_LOG
		uint64_t begin = timer_us_gettime64();
#endif
		pvr_wait_ready();
		pvr_wait_render_done();
#if WITH_PERF_LOG
		uint64_t ready = timer_us_gettime64();
#endif
		copy_scanout(vram, offset, w, h, bgr24);
#if WITH_PERF_LOG
		uint64_t copied = timer_us_gettime64();
#endif

		ymin = (float)y * (float)screen_fh;
		ymax = (float)(y + h) * (float)screen_fh;
		xmin = (float)x * (float)screen_fw;
		xmax = (float)(x + w) * (float)screen_fw;

		pvr_scene_begin();
		pvr_list_begin(PVR_LIST_OP_POLY);

		pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY,
				 PVR_TXRFMT_NONTWIDDLED | (bgr24 ? PVR_TXRFMT_RGB565 : PVR_TXRFMT_ARGB1555),
				 TEX_WIDTH, TEX_HEIGHT, pvram, PVR_FILTER_NONE);

		pvr_poly_compile(&hdr, &cxt);
		pvr_prim(&hdr, sizeof(hdr));

		vert.argb = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
		vert.oargb = 0;
		vert.flags = PVR_CMD_VERTEX;

		vert.x = xmin;
		vert.y = ymin;
		vert.z = 1.0f;
		vert.u = 0.0f;
		vert.v = 0.0f;
		pvr_prim(&vert, sizeof(vert));

		vert.x = xmax;
		vert.y = ymin;
		vert.z = 1.0f;
		vert.u = (float)w / (float)TEX_WIDTH;
		vert.v = 0.0f;
		pvr_prim(&vert, sizeof(vert));

		vert.x = xmin;
		vert.y = ymax;
		vert.z = 1.0f;
		vert.u = 0.0f;
		vert.v = (float)h / (float)TEX_HEIGHT;
		pvr_prim(&vert, sizeof(vert));

		vert.x = xmax;
		vert.y = ymax;
		vert.z = 1.0f;
		vert.u = (float)w / (float)TEX_WIDTH;
		vert.v = (float)h / (float)TEX_HEIGHT;
		vert.flags = PVR_CMD_VERTEX_EOL;
		pvr_prim(&vert, sizeof(vert));

		pvr_list_finish();
		pvr_scene_finish();
#if WITH_PERF_LOG
		scanout_report(begin, ready, copied, offset, x, y, w, h);
#endif
	}

	frame_was_24bpp = bgr24;

	dc_vout_report_stats();
}

static struct rearmed_cbs dc_rearmed_cbs = {
	.pl_vout_open		= dc_vout_open,
	.pl_vout_close		= dc_vout_close,
	.pl_vout_set_mode	= dc_vout_set_mode,
	.pl_vout_flip		= dc_vout_flip,

	.gpu_hcnt		= (unsigned int *)&hSyncCount,
	.gpu_frame_count	= (unsigned int *)&frame_counter,
	.gpu_state_change	= gpu_state_change,

	.gpu_unai = {
		.lighting = 1,
		.blending = 1,
	},
};

void plugin_call_rearmed_cbs(void)
{
	extern void *hGPUDriver;
	void (*rearmed_set_cbs)(const struct rearmed_cbs *cbs);

	rearmed_set_cbs = SysLoadSym(hGPUDriver, "GPUrearmedCallbacks");
	if (rearmed_set_cbs != NULL)
		rearmed_set_cbs(&dc_rearmed_cbs);
}

void pl_frame_limit(void)
{
}
