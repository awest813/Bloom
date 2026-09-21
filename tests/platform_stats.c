#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>

static unsigned int frames, screen_w = 320, screen_h = 240, screen_bpp = 16;
static uint64_t timer_ms, last_cputime, last_idletime;
static bool stats_started;
static uint64_t now, idle;
static unsigned int reports, logs, workload_reports;
static double reported_fps, reported_busy;
typedef struct { uint64_t rnd_last_time; } pvr_stats_t;

static uint64_t timer_ms_gettime64(void) { return now; }
static void *thd_get_idle(void) { return NULL; }
static uint64_t thd_get_cpu_time(void *thread) { return idle; }
static void pvr_get_stats(pvr_stats_t *stats) { stats->rnd_last_time = 3000000; }
static void pvr_perf_report(void) { workload_reports++; }
static void vmu_printf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    reported_fps = va_arg(args, double);
    assert(va_arg(args, unsigned int) == screen_w);
    assert(va_arg(args, unsigned int) == screen_h);
    assert(va_arg(args, unsigned int) == screen_bpp);
    (void)va_arg(args, double);
    reported_busy = va_arg(args, double);
    va_end(args);
    reports++;
}
static int capture_printf(const char *format, ...) { logs++; return 0; }
#define printf capture_printf

/* Production functions are inserted here by test_regressions.py. */

static void expect(double fps, double busy) {
    assert(fabs(reported_fps - fps) < 0.001);
    assert(fabs(reported_busy - busy) < 0.001);
    assert(logs == (WITH_PERF_LOG ? reports : 0));
    assert(workload_reports == (WITH_PERF_LOG && HARDWARE_ACCELERATED ? reports : 0));
}

int main(void) {
    /* Starting at zero must not discard the second presentation baseline. */
    dc_vout_report_stats();
    for (unsigned int i = 1; i <= 50; i++) {
        now = i * 20;
        idle = i * 5;
        dc_vout_report_stats();
        assert(reports == (i == 50));
    }
    expect(50.0, 75.0);

    /* A delayed sample reports rate, not the raw number of callbacks. */
    now += 100; dc_vout_report_stats();
    now += 100; dc_vout_report_stats();
    now += 1300; idle += 750; dc_vout_report_stats();
    assert(reports == 2);
    expect(2.0, 50.0);

    /* A reopened output must establish new baselines, including long uptimes. */
    stats_started = false; frames = 0;
    now = UINT64_C(0x100000000); idle = now / 2;
    dc_vout_report_stats();
    assert(reports == 2);
    now += 1250; dc_vout_report_stats();
    expect(0.8, 100.0);

    /* Scheduler accounting sampled separately can briefly exceed wall time. */
    now += 1000; idle += 1001; dc_vout_report_stats();
    expect(1.0, 0.0);
    return 0;
}
