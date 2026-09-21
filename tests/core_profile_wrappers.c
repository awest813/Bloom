#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static uint64_t mock_us;
static unsigned real_calls;
static uint64_t timer_us_gettime64(void) { return mock_us; }

/* Production profiler is inserted here by test_regressions.py. */

struct lightrec_state { int unused; };
struct xa_decode { int unused; };
static struct lightrec_state state;
static struct xa_decode xa;
static uint32_t command;
static unsigned short samples[2];
static short pcm[2];
static const unsigned char track[3] = {0, 2, 1};
static int sum, last, cmd;

/* Simulate real work while checking the call boundary independently. */
#define WORK(check) do { assert(check); real_calls++; mock_us += 100; } while (0)
uint32_t __real_lightrec_execute(struct lightrec_state *s, uint32_t pc, uint32_t cycles) {
    WORK(s == &state && pc == UINT32_C(0xfffffffc) && cycles == UINT32_C(0x80000001));
    return UINT32_C(0x80001234);
}
uint32_t __real_lightrec_run_interpreter(struct lightrec_state *s, uint32_t pc, uint32_t cycles) {
    WORK(s == &state && pc == 4 && cycles == 9);
    return 12;
}
int __real_do_cmd_list(uint32_t *p, int n, int *s, int *l, int *c) {
    WORK(p == &command && n == 1 && s == &sum && l == &last && c == &cmd);
    *s = 11; *l = 22; *c = 33;
    return -7;
}
void __real_SPUasync(unsigned int cycle, unsigned int flags) { WORK(cycle == 123 && flags == 456); }
void __real_SPUwriteRegister(unsigned long reg, unsigned short value, unsigned int cycle) {
    WORK(reg == 0x1f801c00 && value == 0xfedc && cycle == 123);
}
unsigned short __real_SPUreadRegister(unsigned long reg, unsigned int cycle) {
    WORK(reg == 0x1f801c02 && cycle == 123);
    return 0xabcd;
}
void __real_SPUwriteDMAMem(unsigned short *p, int n, unsigned int cycle) {
    WORK(p == samples && n == 2 && cycle == 123);
}
void __real_SPUreadDMAMem(unsigned short *p, int n, unsigned int cycle) {
    WORK(p == samples && n == 2 && cycle == 123);
    p[1] = 0xbeef;
}
void __real_SPUplayADPCMchannel(struct xa_decode *p, unsigned int cycle, int start) {
    WORK(p == &xa && cycle == 123 && start == 1);
}
int __real_SPUplayCDDAchannel(short *p, int n, unsigned int cycle, int start) {
    WORK(p == pcm && n == 4 && cycle == 123 && start == 0);
    return -2;
}
void __real_psxDma0(uint32_t a, uint32_t b, uint32_t c) { WORK(a == 1 && b == 2 && c == 3); }
void __real_psxDma1(uint32_t a, uint32_t b, uint32_t c) { WORK(a == 4 && b == 5 && c == 6); }
int __real_cdra_readTrack(const unsigned char *p) { WORK(p == track); return -1; }
void __real_GPUupdateLace(void) {
    WORK(1);
    assert(__wrap_do_cmd_list(&command, 1, &sum, &last, &cmd) == -7);
    __wrap_SPUasync(123, 456);
}

int main(void) {
    bloom_profile_reset();
    assert(__wrap_lightrec_execute(&state, UINT32_C(0xfffffffc), UINT32_C(0x80000001)) == UINT32_C(0x80001234));
    assert(__wrap_lightrec_run_interpreter(&state, 4, 9) == 12);
    assert(__wrap_do_cmd_list(&command, 1, &sum, &last, &cmd) == -7);
    assert(sum == 11 && last == 22 && cmd == 33);
    __wrap_SPUasync(123, 456);
    __wrap_SPUwriteRegister(0x1f801c00, 0xfedc, 123);
    assert(__wrap_SPUreadRegister(0x1f801c02, 123) == 0xabcd);
    __wrap_SPUwriteDMAMem(samples, 2, 123);
    __wrap_SPUreadDMAMem(samples, 2, 123);
    assert(samples[1] == 0xbeef);
    __wrap_SPUplayADPCMchannel(&xa, 123, 1);
    assert(__wrap_SPUplayCDDAchannel(pcm, 4, 123, 0) == -2);
    __wrap_psxDma0(1, 2, 3);
    __wrap_psxDma1(4, 5, 6);
    assert(__wrap_cdra_readTrack(track) == -1);
    __wrap_GPUupdateLace();
    assert(real_calls == 16 && frames == 1 && active == OTHER);
    assert(elapsed[PS1] == 200 && elapsed[DRAW] == 200 && elapsed[SOUND] == 800);
    assert(elapsed[VIDEO] == 100 && elapsed[MDEC] == 200 && elapsed[DISC] == 100);
    bloom_profile_stop();
    __wrap_GPUupdateLace();
    assert(real_calls == 19 && frames == 1 && elapsed[VIDEO] == 100);
    return 0;
}
