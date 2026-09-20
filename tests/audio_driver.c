/* Exercise the complete production driver and dfsound output selection. */
#include <assert.h>
#include <stdlib.h>
#include "src/aica_out.c"
#define HAVE_AICA
#include "deps/pcsx_rearmed/plugins/dfsound/out.c"
#include "deps/pcsx_rearmed/plugins/dfsound/nullsnd.c"

SPUConfig spu_config;
static int fail_init, fail_alloc, starts, polls, destroys, shutdowns;
static int sound_shutdowns, drain_on_poll, init_calls;
static snd_stream_callback_t callback;
static int16_t played[PREFILL_SAMPLES];

int snd_stream_init_ex(int channels, size_t bytes)
{
    assert(channels == 2 && bytes == STREAM_CHN_BYTES);
    init_calls++;
    return fail_init ? -1 : 0;
}

snd_stream_hnd_t snd_stream_alloc(snd_stream_callback_t cb, int bytes)
{
    assert(bytes == STREAM_CHN_BYTES);
    callback = cb;
    return fail_alloc ? SND_STREAM_INVALID : 0;
}

void snd_stream_start(snd_stream_hnd_t hnd, uint32_t freq, int stereo)
{
    int got;
    assert(hnd == 0 && freq == 44100 && stereo == 1);
    /* Match KOS: start preloads two half-buffer callbacks synchronously. */
    for (int half = 0; half < 2; half++) {
        void *data = callback(hnd, STREAM_CHN_BYTES, &got);
        assert(got == STREAM_CHN_BYTES);
        memcpy(played + half * BOUNCE_SAMPLES, data, got);
    }
    starts++;
}

void snd_stream_volume(snd_stream_hnd_t hnd, int volume)
{
    assert(hnd == 0 && starts > 0 && volume == 255);
}

int snd_stream_poll(snd_stream_hnd_t hnd)
{
    int got;
    assert(hnd == 0 && stream_started);
    polls++;
    if (drain_on_poll) {
        callback(hnd, drain_on_poll, &got);
        assert(got == drain_on_poll);
        drain_on_poll = 0;
    }
    return 0;
}

void snd_stream_destroy(snd_stream_hnd_t hnd) { assert(hnd == 0); destroys++; }
void snd_stream_shutdown(void) { shutdowns++; }
void snd_shutdown(void) { sound_shutdowns++; }

int main(void)
{
    int16_t input[RING_SAMPLES];
    int got;
    for (int i = 0; i < RING_SAMPLES; i++) input[i] = i;

    /* Failed setup must release partial resources and select silent output. */
    fail_init = 1;
    SetupSound();
    assert(strcmp(out_current->name, "none") == 0);
    assert(shutdowns == 1 && sound_shutdowns == 1 && destroys == 0);
    fail_init = 0; fail_alloc = 1;
    SetupSound();
    assert(strcmp(out_current->name, "none") == 0);
    assert(shutdowns == 2 && sound_shutdowns == 2 && destroys == 0);
    fail_alloc = 0;
    SetupSound();
    assert(strcmp(out_current->name, "aica") == 0);
    assert(init_calls == 3 && starts == 0);
    assert(aica_init() == 0 && init_calls == 3);
    aica_busy();
    assert(polls == 0); /* Polling an unstarted KOS stream asserts. */
    aica_feed(NULL, 100);
    aica_feed(input, -1);
    assert(ring_count() == 0 && starts == 0);

    aica_feed(input, PREFILL_SAMPLES * 2 - 4);
    assert(starts == 0);
    aica_feed(input + PREFILL_SAMPLES - 2, 4);
    assert(starts == 1 && ring_count() == 0);
    assert(memcmp(played, input, sizeof(played)) == 0);

    /* Distinct samples detect reordering as well as channel misalignment. */
    for (int pass = 0; pass < 10; pass++) {
        aica_feed(input, sizeof(input));
        assert(ring_count() == RING_SAMPLES - 2);
        for (int offset = 0; offset < RING_SAMPLES; offset += BOUNCE_SAMPLES) {
            aica_callback(0, STREAM_CHN_BYTES, &got);
            assert(got == STREAM_CHN_BYTES);
            for (int i = 0; i < BOUNCE_SAMPLES; i++)
                assert(bounce[i] == (offset + i < RING_SAMPLES - 2 ? input[offset + i] : 0));
        }
        assert(ring_count() == 0);
    }

    /* Recover space before dropping a batch that playback can accept. */
    aica_feed(input, (RING_SAMPLES - 2) * 2);
    drain_on_poll = STREAM_CHN_BYTES;
    aica_feed(input, STREAM_CHN_BYTES);
    assert(ring_count() == RING_SAMPLES - 2);
    while (ring_count()) aica_callback(0, STREAM_CHN_BYTES, &got);
    aica_feed(input, 7);
    assert(ring_count() == 2);
    aica_callback(0, 16, &got);
    assert(got == 16 && bounce[0] == 0 && bounce[1] == 1);
    for (int i = 2; i < 8; i++) assert(bounce[i] == 0);
    aica_callback(0, -1, &got); assert(got == 0);
    aica_callback(0, 100000, &got); assert(got == STREAM_CHN_BYTES);

    aica_feed(input, 400);
    aica_finish();
    assert(ring_count() == 0 && !stream_started && destroys == 1);
    aica_finish();
    assert(shutdowns == 3 && sound_shutdowns == 3 && destroys == 1);
    aica_feed(input, sizeof(input));
    assert(ring_count() == 0);
    SetupSound();
    assert(!stream_started && ring_count() == 0);
    aica_feed(input, PREFILL_SAMPLES * 2);
    assert(starts == 2 && memcmp(played, input, sizeof(played)) == 0);
    out_current->finish();
    assert(shutdowns == 4 && destroys == 2);
    return 0;
}
