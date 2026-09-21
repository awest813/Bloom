/* Exercise the complete production driver and dfsound output selection. */
#include <assert.h>
#include <stdlib.h>
static int force_silent;
int bloom_want_silent_audio(void) { return force_silent; }
#include "src/aica_out.c"
#define HAVE_AICA
#include "deps/pcsx_rearmed/plugins/dfsound/out.c"
#include "deps/pcsx_rearmed/plugins/dfsound/nullsnd.c"

SPUConfig spu_config;
static int fail_init, fail_alloc, starts, polls, destroys, shutdowns;
static int sound_shutdowns, drain_on_poll, init_calls;
static int poll_error;
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
    if (poll_error) return poll_error;
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
        aica_feed(input, (RING_SAMPLES - 2) * 2);
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

    /* A full ring plus a new mix must keep the newest samples, not the oldest. */
    {
        int16_t newer[256];
        int skip;

        for (int i = 0; i < 256; i++)
            newer[i] = (int16_t)(20000 + i);
        aica_feed(input, (RING_SAMPLES - 2) * 2);
        aica_feed(newer, sizeof(newer));
        assert(ring_count() == RING_SAMPLES - 2);
        aica_callback(0, STREAM_CHN_BYTES, &got);
        assert(got == STREAM_CHN_BYTES && bounce[0] == 256);
        skip = ring_count() - 256;
        while (skip > 0) {
            int chunk = skip > BOUNCE_SAMPLES ? BOUNCE_SAMPLES : skip;
            aica_callback(0, chunk * 2, &got);
            skip -= got / 2;
        }
        aica_callback(0, 512, &got);
        assert(got == 512 && bounce[0] == 20000 && bounce[255] == 20255);
        while (ring_count())
            aica_callback(0, STREAM_CHN_BYTES, &got);
    }

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

    force_silent = 1;
    {
        int inits = init_calls;
        int closed = destroys;
        SetupSound();
        assert(strcmp(out_current->name, "none") == 0);
        assert(init_calls == inits);
        assert(destroys == closed + 1);
    }
    force_silent = 0;

    /* A batch larger than the ring retains complete, newest stereo frames. */
    SetupSound();
    {
        int16_t large[RING_SAMPLES + 256];
        const int count = sizeof(large) / sizeof(large[0]);
        const int skipped = count - (RING_SAMPLES - 2);
        for (int i = 0; i < count; i++) large[i] = (int16_t)(i - 8192);
        aica_feed(large, sizeof(large));
        assert(memcmp(played, large + skipped, sizeof(played)) == 0);
        int offset = skipped + PREFILL_SAMPLES;
        while (ring_count()) {
            int amount = ring_count() > BOUNCE_SAMPLES ? BOUNCE_SAMPLES : ring_count();
            aica_callback(0, amount * 2, &got);
            assert(got == amount * 2);
            assert(memcmp(bounce, large + offset, got) == 0);
            offset += amount;
        }
        assert(offset == count);
    }

    /* Poll failures must close once, discard pending PCM, and never restart
     * an invalid handle from feed's overflow or startup paths. */
    for (int path = 0; path < 3; path++) {
        if (path) {
            SetupSound();
            aica_feed(input, PREFILL_SAMPLES * 2);
        }
        int closed = destroys, stopped = shutdowns, started = starts;
        if (path == 2) aica_feed(input, (RING_SAMPLES - 2) * 2);
        poll_error = -(path + 1);
        if (path == 0) aica_busy();
        else if (path == 1) aica_feed(NULL, 0);
        else aica_feed(input, 16);
        assert(!stream_initialized && !stream_started && ring_count() == 0);
        assert(stream_hnd == SND_STREAM_INVALID);
        assert(destroys == closed + 1 && shutdowns == stopped + 1);
        aica_feed(input, sizeof(input));
        aica_busy();
        aica_finish();
        assert(starts == started && destroys == closed + 1);
        poll_error = 0;
    }
    SetupSound();
    aica_feed(input, PREFILL_SAMPLES * 2);
    assert(stream_started && memcmp(played, input, sizeof(played)) == 0);
    aica_finish();
    return 0;
}
