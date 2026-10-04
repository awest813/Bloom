#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
static uint64_t mock_us;
static unsigned int reports;
static double percentages[7], vblank_rate;
static uint64_t timer_us_gettime64(void) { return mock_us; }
static int capture_profile(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vblank_rate = va_arg(ap, double);
    double sum = 0;
    for (int i = 0; i < 7; i++) {
        percentages[i] = va_arg(ap, double);
        sum += percentages[i];
    }
    va_end(ap);
    assert(fabs(sum - 100) < 0.000001);
    reports++;
    mock_us += 100000; /* Serial output must not enter the next sample. */
    return 0;
}
#define printf capture_profile

/* Production accounting is inserted here by test_regressions.py. */

int main(void) {
    bloom_profile_present();
    assert(!presentations);
    enum profile_part previous = profile_enter(DRAW);
    profile_leave(previous);
    profile_report();
    assert(!reports && !frames && !calls[DRAW]);

    mock_us = UINT64_C(0x100000000);
    bloom_profile_reset();
    uint64_t start = mock_us;
    bloom_profile_present();
    mock_us += 1000;
    previous = profile_enter(PS1);
    mock_us += 1000;
    enum profile_part cpu = profile_enter(DRAW);
    mock_us += 1500;
    enum profile_part draw = profile_enter(SOUND);
    mock_us += 500;
    profile_leave(draw);
    mock_us += 1000;
    profile_leave(cpu);
    mock_us += 1000;
    profile_leave(previous);
    assert(elapsed[PS1] == 2000 && elapsed[DRAW] == 2500);
    assert(elapsed[SOUND] == 500 && elapsed[OTHER] == 1000);
    assert(active == OTHER);
    profile_report();
    assert(!reports);
    mock_us = start + 5000000;
    profile_report();
    assert(reports == 1 && fabs(vblank_rate - 0.4) < 0.000001);
    assert(fabs(percentages[0] - 0.04) < 0.000001);
    assert(fabs(percentages[1] - 0.05) < 0.000001);
    assert(!frames && !calls[PS1] && !elapsed[DRAW]);
    assert(epoch == mock_us && stamp == mock_us);
    assert(bloom_profile_sample.sequence == 2);
    assert(bloom_profile_sample.duration_us == 5000000);
    assert(bloom_profile_sample.vblanks == 2);
    assert(bloom_profile_sample.presentations == 1 && presentations == 0);
    assert(bloom_profile_sample.usec[PS1] == 2000);
    assert(bloom_profile_sample.usec[DRAW] == 2500);
    assert(bloom_profile_sample.calls[SOUND] == 1);

    /* A report in a nested call must retain the current category. */
    previous = profile_enter(PS1);
    mock_us += 5000000;
    profile_report();
    assert(reports == 2 && fabs(percentages[0] - 100) < 0.000001);
    assert(active == PS1);
    mock_us += 1000;
    profile_leave(previous);
    assert(elapsed[PS1] == 1000);
    bloom_profile_stop();
    bloom_profile_present();
    assert(!presentations);
    mock_us += 10000000; /* Menu/teardown time must not produce a sample. */
    previous = profile_enter(SOUND);
    profile_leave(previous);
    profile_report();
    assert(reports == 2 && elapsed[PS1] == 1000 && !calls[SOUND]);
    bloom_profile_reset();
    assert(active == OTHER && !elapsed[PS1]);
    /* Restart leaves the old serial intact until the next complete sample. */
    assert(bloom_profile_sample.sequence == 4);
    previous = profile_enter(PS1);
    enum profile_part sub = profile_enter(INTERPRETER);
    mock_us += 2000;
    profile_leave(sub);
    sub = profile_enter(GEOMETRY);
    mock_us += 1000;
    profile_leave(sub);
    sub = profile_enter(IO);
    mock_us += 1000;
    profile_leave(sub);
    mock_us += 4996000;
    profile_leave(previous);
    profile_report();
    assert(bloom_profile_sample.sequence == 6);
    assert(fabs(percentages[0] - 100) < 0.000001);
    assert(bloom_profile_sample.usec[INTERPRETER] == 2000);
    assert(bloom_profile_sample.usec[GEOMETRY] == 1000);
    assert(bloom_profile_sample.usec[IO] == 1000);
    return 0;
}
