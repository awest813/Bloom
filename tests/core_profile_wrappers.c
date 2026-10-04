#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static uint64_t mock_us;
static unsigned real_calls;
static uint64_t timer_us_gettime64(void) { return mock_us; }

/* Production profiler is inserted here by test_regressions.py. */

struct lightrec_state { int unused; };
struct block { int unused; };
struct psxCP2Regs { unsigned value; };
struct xa_decode { int unused; };
static struct lightrec_state state;
static struct block block;
static struct psxCP2Regs geometry;
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
    assert(__wrap_lightrec_emulate_block(s, &block, pc) == 0x80000004);
    __wrap_gteRTPS(&geometry);
    return UINT32_C(0x80001234);
}
uint32_t __real_lightrec_emulate_block(struct lightrec_state *s, struct block *b, uint32_t pc) {
    WORK(s == &state && b == &block && pc == UINT32_C(0xfffffffc));
    __wrap_gteNCLIP(&geometry);
    return 0x80000004;
}
#define REAL_GTE(name) void __real_##name(struct psxCP2Regs *r) { \
    WORK(r == &geometry); r->value++; }
REAL_GTE(gteRTPS) REAL_GTE(gteNCLIP) REAL_GTE(gteOP) REAL_GTE(gteDPCS)
REAL_GTE(gteINTPL) REAL_GTE(gteMVMVA) REAL_GTE(gteNCDS) REAL_GTE(gteCDP)
REAL_GTE(gteNCDT) REAL_GTE(gteNCCS) REAL_GTE(gteCC) REAL_GTE(gteNCS)
REAL_GTE(gteNCT) REAL_GTE(gteSQR) REAL_GTE(gteDCPL) REAL_GTE(gteDPCT)
REAL_GTE(gteAVSZ3) REAL_GTE(gteAVSZ4) REAL_GTE(gteRTPT) REAL_GTE(gteGPF)
REAL_GTE(gteGPL) REAL_GTE(gteNCCT)
#undef REAL_GTE
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
void __real_renderer_sync(void) { WORK(1); }
void __real_renderer_sync_ecmds(uint32_t *p) { WORK(p == &command); }
void __real_renderer_flush_queues(void) { WORK(1); }
void __real_renderer_update_caches(int x,int y,int w,int h,int changed) {
    WORK(x == 1 && y == 2 && w == 3 && h == 4 && changed == 5);
}
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
uint8_t __real_psxHwRead8(uint32_t address) {
    WORK(address == 0x1f801800); return 0xfe;
}
uint16_t __real_psxHwRead16(uint32_t address) {
    WORK(address == 0x1f801c02);
    return __wrap_SPUreadRegister(address, 123);
}
uint32_t __real_psxHwRead32(uint32_t address) {
    WORK(address == 0x1f801814); return UINT32_C(0xfedcba98);
}
#define REAL_WRITE(width) \
void __real_psxHwWrite##width(uint32_t address, uint32_t value) { \
    WORK(address == 0x1f801800 && value == UINT32_C(0xfedcba98)); }
REAL_WRITE(8) REAL_WRITE(16) REAL_WRITE(32)
#undef REAL_WRITE

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
    assert(real_calls == 19 && frames == 1 && active == OTHER);
    assert(elapsed[INTERPRETER] == 100 && elapsed[GEOMETRY] == 200);
    assert(geometry.value == 2);
    assert(gte_calls[0] == 1 && gte_calls[1] == 1);
    assert(gte_elapsed[0] == 100 && gte_elapsed[1] == 100);
    unsigned slot = ((UINT32_C(0xfffffffc) >> 2) ^ (UINT32_C(0xfffffffc) >> 10))
        & (BLOOM_DISPATCH_PROFILE_SLOTS - 1);
    assert(dispatch_entries[slot].pc == UINT32_C(0xfffffffc));
    assert(dispatch_entries[slot].calls == 1 && dispatch_entries[slot].usec == 100);
    assert(dispatch_entries[slot].requested_min == UINT32_C(0x80000001));
    assert(dispatch_entries[slot].requested_max == UINT32_C(0x80000001));
    assert(elapsed[PS1] == 200 && elapsed[DRAW] == 200 && elapsed[SOUND] == 800);
    assert(elapsed[VIDEO] == 100 && elapsed[MDEC] == 200 && elapsed[DISC] == 100);
    bloom_profile_stop();
    __wrap_GPUupdateLace();
    assert(real_calls == 22 && frames == 1 && elapsed[VIDEO] == 100);
    __wrap_gteRTPS(&geometry);
    assert(geometry.value == 3 && elapsed[GEOMETRY] == 200);
    assert(gte_calls[0] == 1 && gte_elapsed[0] == 100);
    bloom_profile_reset();
    assert(gte_calls[0] == 0 && gte_elapsed[0] == 0);
    assert(dispatch_entries[slot].calls == 0);
    assert(__wrap_psxHwRead8(0x1f801800) == 0xfe);
    assert(__wrap_psxHwRead16(0x1f801c02) == 0xabcd);
    assert(__wrap_psxHwRead32(0x1f801814) == UINT32_C(0xfedcba98));
    __wrap_psxHwWrite8(0x1f801800, UINT32_C(0xfedcba98));
    __wrap_psxHwWrite16(0x1f801800, UINT32_C(0xfedcba98));
    __wrap_psxHwWrite32(0x1f801800, UINT32_C(0xfedcba98));
    assert(elapsed[IO] == 600 && calls[IO] == 6);
    assert(elapsed[SOUND] == 100 && calls[SOUND] == 1 && active == OTHER);
    __wrap_gteRTPS(&geometry);
    mock_us += 5000000;
    profile_report();
    assert(bloom_gte_profile_sample.sequence == bloom_profile_sample.sequence);
    assert(bloom_gte_profile_sample.duration_us == bloom_profile_sample.duration_us);
    assert(bloom_gte_profile_sample.calls[0] == 1);
    assert(bloom_gte_profile_sample.usec[0] == 100);
    assert(gte_calls[0] == 0 && gte_elapsed[0] == 0);
    assert(bloom_dispatch_profile_sample.sequence == bloom_profile_sample.sequence);
    assert(bloom_dispatch_profile_sample.duration_us == bloom_profile_sample.duration_us);
    assert(bloom_io_profile_sample.sequence == bloom_profile_sample.sequence);
    assert(bloom_io_profile_sample.duration_us == bloom_profile_sample.duration_us);
    unsigned io_seen = 0;
    for (unsigned i = 0; i < BLOOM_IO_PROFILE_SLOTS; i++) {
        const volatile struct bloom_io_entry *entry = &bloom_io_profile_sample.entries[i];
        if (!entry->calls) continue;
        unsigned kind = entry->key >> 16, offset = entry->key & 65535;
        assert(kind < 6 && !(io_seen & (1u << kind)));
        assert(offset == (kind == 1 ? 0x1c00 : kind == 2 ? 0x1814 : 0x1800));
        assert(entry->calls == 1 && entry->usec == 100);
        assert(io_entries[i].calls == 0);
        io_seen |= 1u << kind;
    }
    assert(io_seen == 63 && bloom_io_profile_sample.dropped_calls == 0);
    for (unsigned i = 0; i < BLOOM_IO_PROFILE_SLOTS; i++) {
        io_entries[i].calls = 1;
        io_entries[i].key = i;
    }
    io_record(5, 0x1f801810, 0, 123);
    assert(io_dropped_calls == 1 && io_dropped_us == 123);
    memset(io_entries, 0, sizeof(io_entries));
    const uint32_t dma_values[] = {0x01000401, 0x01000201, 0x01000200, 0};
    for (unsigned i = 0; i < 4; i++) io_record(5, 0x1f8010a8, dma_values[i], 100+i);
    unsigned modes = 0;
    for (unsigned i = 0; i < BLOOM_IO_PROFILE_SLOTS; i++) {
        if (!io_entries[i].calls) continue;
        unsigned mode = io_entries[i].key >> 24;
        assert(mode >= 1 && mode <= 4 && !(modes & (1u << mode)));
        assert((io_entries[i].key & 0xffffff) == 0x510a8);
        assert(io_entries[i].calls == 1 && io_entries[i].usec == 99 + mode);
        modes |= 1u << mode;
    }
    assert(modes == 30);
    dispatch_record(0xfffffffc, 100, 11);
    dispatch_record(0xfffffffc, 50, 12);
    assert(dispatch_entries[slot].calls == 2 && dispatch_entries[slot].usec == 23);
    assert(dispatch_entries[slot].requested_min == 50 && dispatch_entries[slot].requested_max == 100);
    for (unsigned i = 0; i < BLOOM_DISPATCH_PROFILE_SLOTS; i++) {
        dispatch_entries[i].calls = 1;
        dispatch_entries[i].pc = i * 4;
    }
    dispatch_record(0xdeadbeef, 10, 123);
    assert(dispatch_dropped_calls == 1 && dispatch_dropped_us == 123);
    bloom_profile_reset(); bloom_profile_stop();
    assert(__wrap_lightrec_execute(&state, UINT32_C(0xfffffffc), UINT32_C(0x80000001))
           == UINT32_C(0x80001234));
    assert(dispatch_entries[slot].calls == 0 && dispatch_dropped_calls == 0);
    bloom_profile_reset();
    __wrap_renderer_sync();
    __wrap_renderer_sync_ecmds(&command);
    __wrap_renderer_flush_queues();
    __wrap_renderer_update_caches(1,2,3,4,5);
    assert(elapsed[DRAW] == 400 && calls[DRAW] == 4 && active == OTHER);
    return 0;
}
