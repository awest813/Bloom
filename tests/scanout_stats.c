#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>

static uint64_t scanout_wait_us, scanout_copy_us, scanout_submit_us;
static uint64_t scanout_report_ms, now_us;
static unsigned int scanout_samples, reports, samples;
static void *pvram;
static double wait_ms, copy_ms, submit_ms;
static uint64_t timer_ms_gettime64(void) { return now_us / 1000; }
static uint64_t timer_us_gettime64(void) { return now_us; }
#define PVR_GET(reg) 0x01df0000
static int capture_printf(const char *format, ...) {
    va_list ap;
    va_start(ap, format);
    wait_ms = va_arg(ap, double);
    copy_ms = va_arg(ap, double);
    submit_ms = va_arg(ap, double);
    samples = va_arg(ap, unsigned int);
    va_end(ap);
    reports++;
    return 0;
}
#define printf capture_printf

/* Production functions are inserted here by test_regressions.py. */

int main(void) {
    now_us = 4000000;
    scanout_report(now_us - 4000, now_us - 3000, now_us - 500, 0, 0, 0, 320, 240);
    assert(!reports && scanout_samples == 1);
    now_us = 5100000;
    scanout_report(now_us - 8000, now_us - 5000, now_us - 1500, 0, 0, 0, 320, 240);
    assert(reports == 1 && samples == 2);
    assert(fabs(wait_ms - 2) < 0.000001);
    assert(fabs(copy_ms - 3) < 0.000001);
    assert(fabs(submit_ms - 1) < 0.000001);
    assert(!scanout_samples && !scanout_wait_us && !scanout_copy_us && !scanout_submit_us);
    assert(scanout_report_ms == 5100);

    /* Long uptime and an irregular reporting interval must not wrap or
     * divide by elapsed wall time instead of the number of samples. */
    now_us = UINT64_C(0x100000000) * 1000;
    scanout_report(now_us - 9000, now_us - 8000, now_us - 2000, 0, 0, 0, 640, 480);
    assert(reports == 2 && samples == 1);
    assert(fabs(copy_ms - 6) < 0.000001);
    assert(fabs(submit_ms - 2) < 0.000001);
    return 0;
}
