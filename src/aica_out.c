// SPDX-License-Identifier: GPL-2.0-only
/*
 * dfsound output driver: push mixed S16 stereo 44100 into an AICA stream.
 *
 * Copyright (C) 2026 Bloom contributors
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dc/sound/sound.h>
#include <dc/sound/stream.h>

#include "out.h"
#include "settings.h"
#include "spu_config.h"

/* Per-channel AICA buffer. 8 KiB of S16 holds ~93 ms at 44100 Hz.
 * This cushions frame-time variation but cannot fix sustained slow emulation. */
#define STREAM_CHN_BYTES  8192
#define RING_SAMPLES      16384 /* interleaved S16, power of two */
#define BOUNCE_SAMPLES    (STREAM_CHN_BYTES / 2)
/* Two channels of S16: one full AICA buffer before starting playback. */
#define PREFILL_SAMPLES   STREAM_CHN_BYTES

_Static_assert((RING_SAMPLES & (RING_SAMPLES - 1)) == 0,
	       "ring size must be a power of two");
_Static_assert(PREFILL_SAMPLES <= RING_SAMPLES - 2,
	       "ring must hold the complete startup prefill");

static snd_stream_hnd_t stream_hnd = SND_STREAM_INVALID;
static bool stream_initialized, stream_started;
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
	/* Reserve a stereo frame so full and empty stay distinct. */
	return RING_SAMPLES - 2 - ring_count();
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

static void ring_drop(int n)
{
	int have = ring_count();

	if (n < 0)
		n = 0;
	n &= ~1;
	if (n > have)
		n = have & ~1;
	ring_r = (ring_r + n) & (RING_SAMPLES - 1);
}

/* KOS documents smp_req as "samples" but snd_stream_fill passes bytes. */
static void *aica_callback(snd_stream_hnd_t hnd, int needed_bytes, int *got_bytes)
{
	int want, have;

	(void)hnd;

	if (!got_bytes)
		return bounce;

	want = needed_bytes / 2;
	if (want > BOUNCE_SAMPLES)
		want = BOUNCE_SAMPLES;
	if (want < 0)
		want = 0;
	want &= ~1; /* Never split a left/right pair. */

	have = ring_count();
	if (have > want)
		have = want;

	ring_read(bounce, have);
	/* Fill partial underruns too; never replay stale buffer contents. */
	memset(bounce + have, 0, (want - have) * sizeof(*bounce));

	*got_bytes = want * 2;
	return bounce;
}

static void aica_finish(void);

static int aica_init(void)
{
	if (bloom_want_silent_audio()) {
		aica_finish();
		return -1;
	}

	if (stream_initialized)
		return 0;

	/* Voices + XA + CDDA mixed on SH-4; skip reverb/gauss on this CPU. */
	spu_config.iVolume = 768;
	spu_config.iUseReverb = 0;
	spu_config.iUseInterpolation = 0;
	spu_config.iUseThread = 0;
	spu_config.iTempo = 0;
	spu_config.iXAPitch = 0;

	ring_r = ring_w = 0;
	stream_started = false;
	/* KOS can allocate buffers before reporting an initialization failure. */
	stream_initialized = true;
	if (snd_stream_init_ex(2, STREAM_CHN_BYTES)) {
		fprintf(stderr, "AICA: stream initialization failed; using silent output\n");
		aica_finish();
		return -1;
	}

	stream_hnd = snd_stream_alloc(aica_callback, STREAM_CHN_BYTES);
	if (stream_hnd == SND_STREAM_INVALID) {
		fprintf(stderr, "AICA: stream allocation failed; using silent output\n");
		aica_finish();
		return -1;
	}

	/* Start from feed() once both AICA channel buffers can be filled. */
	return 0;
}

static void aica_finish(void)
{
	if (!stream_initialized)
		return;

	if (stream_hnd != SND_STREAM_INVALID) {
		/* destroy also stops the stream and waits for pending DMA. */
		snd_stream_destroy(stream_hnd);
		stream_hnd = SND_STREAM_INVALID;
	}

	snd_stream_shutdown();
	snd_shutdown();
	stream_initialized = stream_started = false;
	ring_r = ring_w = 0;
}

static int aica_busy(void)
{
	if (stream_started)
		snd_stream_poll(stream_hnd);

	/* dfsound uses this only for tempo adjustment, not producer throttling. */
	return ring_count() > RING_SAMPLES / 2;
}

static void aica_feed(void *data, int bytes)
{
	const int16_t *src = data;
	int samples, space, cap = RING_SAMPLES - 2;

	if (stream_hnd == SND_STREAM_INVALID)
		return;

	if (bytes < 4 || data == NULL)
		goto poll;

	samples = (bytes / 2) & ~1;
	/* Keep the newest samples when a mix is larger than the ring. */
	if (samples > cap) {
		src += samples - cap;
		samples = cap;
	}
	/* Let playback consume queued audio, then drop the oldest leftover
	 * so a slow frame does not make the mix lag further behind. */
	if (samples > ring_space() && stream_started)
		snd_stream_poll(stream_hnd);
	space = ring_space();
	if (samples > space)
		ring_drop(samples - space);
	space = ring_space();
	if (samples > space)
		samples = space;

	ring_write(src, samples);

poll:
	if (!stream_started && ring_count() >= PREFILL_SAMPLES) {
		snd_stream_start(stream_hnd, 44100, 1);
		stream_started = true;
		snd_stream_volume(stream_hnd, 255);
	}
	if (stream_started)
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
