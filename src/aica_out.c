// SPDX-License-Identifier: GPL-2.0-only
/*
 * dfsound output driver: push mixed S16 stereo 44100 into an AICA stream.
 *
 * Copyright (C) 2026 Bloom contributors
 */

#include <stdint.h>
#include <string.h>

#include <dc/sound/sound.h>
#include <dc/sound/stream.h>

#include "out.h"
#include "spu_config.h"

/* Per-channel AICA buffer. 8 KiB of S16 is ~93 ms at 44100 Hz; long enough
 * for slow 3D frames without blowing the 2 MiB sound RAM budget. */
#define STREAM_CHN_BYTES  8192
#define RING_SAMPLES      8192 /* interleaved S16, power of two */
#define BOUNCE_SAMPLES    8192

_Static_assert((RING_SAMPLES & (RING_SAMPLES - 1)) == 0,
	       "ring size must be a power of two");

static snd_stream_hnd_t stream_hnd = SND_STREAM_INVALID;
static int ring_r, ring_w;
static __attribute__((aligned(32))) int16_t ring[RING_SAMPLES];
static __attribute__((aligned(32))) int16_t bounce[BOUNCE_SAMPLES];

static int ring_count(void)
{
	int n = ring_w - ring_r;

	if (n < 0)
		n += RING_SAMPLES;
	return n;
}

static int ring_space(void)
{
	/* Leave one slot empty so full and empty stay distinct. */
	return RING_SAMPLES - 1 - ring_count();
}

static void ring_write(const int16_t *src, int n)
{
	int first;

	while (n > 0) {
		first = RING_SAMPLES - ring_w;
		if (first > n)
			first = n;
		memcpy(ring + ring_w, src, first * sizeof(*ring));
		ring_w = (ring_w + first) & (RING_SAMPLES - 1);
		src += first;
		n -= first;
	}
}

static void ring_read(int16_t *dst, int n)
{
	int first;

	while (n > 0) {
		first = RING_SAMPLES - ring_r;
		if (first > n)
			first = n;
		memcpy(dst, ring + ring_r, first * sizeof(*ring));
		ring_r = (ring_r + first) & (RING_SAMPLES - 1);
		dst += first;
		n -= first;
	}
}

/* KOS documents smp_req as "samples" but snd_stream_fill passes bytes. */
static void *aica_callback(snd_stream_hnd_t hnd, int needed_bytes, int *got_bytes)
{
	int want, have;

	(void)hnd;

	want = needed_bytes / 2;
	if (want > BOUNCE_SAMPLES)
		want = BOUNCE_SAMPLES;
	if (want < 0)
		want = 0;

	have = ring_count();
	if (have <= 0) {
		memset(bounce, 0, want * 2);
		*got_bytes = want * 2;
		return bounce;
	}
	if (want > have)
		want = have;

	ring_read(bounce, want);

	*got_bytes = want * 2;
	return bounce;
}

static int aica_init(void)
{
	/* Voices + XA + CDDA mixed on SH-4; skip reverb/gauss on this CPU. */
	spu_config.iVolume = 768;
	spu_config.iUseReverb = 0;
	spu_config.iUseInterpolation = 0;
	spu_config.iUseThread = 0;
	spu_config.iTempo = 0;
	spu_config.iXAPitch = 0;

	if (snd_stream_init())
		return -1;

	stream_hnd = snd_stream_alloc(aica_callback, STREAM_CHN_BYTES);
	if (stream_hnd == SND_STREAM_INVALID) {
		snd_stream_shutdown();
		return -1;
	}

	ring_r = ring_w = 0;
	snd_stream_volume(stream_hnd, 255);
	snd_stream_start(stream_hnd, 44100, 1);
	return 0;
}

static void aica_finish(void)
{
	if (stream_hnd != SND_STREAM_INVALID) {
		snd_stream_stop(stream_hnd);
		snd_stream_destroy(stream_hnd);
		stream_hnd = SND_STREAM_INVALID;
	}

	snd_stream_shutdown();
	snd_shutdown();
}

static int aica_busy(void)
{
	if (stream_hnd != SND_STREAM_INVALID)
		snd_stream_poll(stream_hnd);

	/* Back-pressure only used if iTempo is enabled. */
	return ring_count() > RING_SAMPLES / 2;
}

static void aica_feed(void *data, int bytes)
{
	const int16_t *src = data;
	int samples, space;

	if (bytes < 2 || data == NULL)
		goto poll;

	samples = bytes / 2;
	space = ring_space();
	if (samples > space)
		samples = space;

	ring_write(src, samples);

poll:
	if (stream_hnd != SND_STREAM_INVALID)
		snd_stream_poll(stream_hnd);
}

void out_register_aica(struct out_driver *drv)
{
	drv->name = "aica";
	drv->init = aica_init;
	drv->finish = aica_finish;
	drv->busy = aica_busy;
	drv->feed = aica_feed;
}
