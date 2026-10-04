/* Standalone kernel benchmark; not a measurement of game FPS. */
#include <stdint.h>
#include <stdio.h>
#include "deps/pcsx_rearmed/libpcsxcore/gpu.h"
#ifdef __sh__
#include <kos.h>
#include <arch/timer.h>
static uint64_t now_us(void) { return timer_us_gettime64(); }
#else
#include <time.h>
static uint64_t now_us(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}
#endif
typedef uint32_t (*poll_fn)(uint32_t, uint32_t, uint32_t);
static __attribute__((noinline)) uint32_t reference(uint32_t status,
                                                  uint32_t stride, uint32_t limit) {
    uint32_t cycle = 100, old = gpuStatusAtCycle(status, cycle, 500, 0), val = old;
    while (limit > cycle && val == old) {
        cycle += stride;
        val = gpuStatusAtCycle(status, cycle, 500, 0);
    }
    return cycle;
}
static __attribute__((noinline)) uint32_t candidate(uint32_t status,
                                                  uint32_t stride, uint32_t limit) {
    uint32_t cycle = 100, old = gpuStatusAtCycle(status, cycle, 500, 0), val = old;
    cycle = gpuConstantPollEnd(status, cycle, limit, stride, 0);
    while (limit > cycle && val == old) {
        cycle += stride;
        val = gpuStatusAtCycle(status, cycle, 500, 0);
    }
    return cycle;
}
static volatile uint32_t checksum;
static uint64_t run(poll_fn volatile fn, uint32_t status, uint32_t stride) {
    uint64_t start = now_us();
    uint32_t sum = 0;
    for (unsigned i = 0; i < 10000; i++) sum += fn(status, stride, 2200);
    checksum = sum;
    return now_us() - start;
}
int main(void) {
#ifdef __sh__
    dbgio_dev_select("fb");
#endif
    const uint32_t strides[] = {1, 4, 16, 64, 256};
    for (unsigned changing = 0; changing < 2; changing++) {
        uint32_t status = PSXGPU_nBUSY | (changing ? 0 : PSXGPU_LCF);
        for (unsigned s = 0; s < 5; s++) {
            uint32_t stride = strides[s];
            if (reference(status, stride, 2200) != candidate(status, stride, 2200)) return 1;
            run(reference, status, stride); run(candidate, status, stride);
            uint64_t old_total = 0, new_total = 0;
            for (unsigned pass = 0; pass < 2; pass++) {
                if (pass) {
                    new_total += run(candidate, status, stride);
                    old_total += run(reference, status, stride);
                } else {
                    old_total += run(reference, status, stride);
                    new_total += run(candidate, status, stride);
                }
            }
            printf("C%u S%lu old=%llu new=%llu us\n", changing, (unsigned long)stride,
                   (unsigned long long)old_total, (unsigned long long)new_total);
        }
    }
    puts("Poll comparisons passed");
#ifdef __sh__
    for (;;) thd_sleep(1000);
#endif
    return 0;
}
