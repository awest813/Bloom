#include <assert.h>
#include <stdint.h>
#include "deps/pcsx_rearmed/libpcsxcore/gpu.h"
#undef HW_GPU_STATUS
typedef uint32_t u32;
struct lightrec_state { int unused; };
static struct { u32 cycle, next_interupt, gpuIdleAfter; } psxRegs;
static u32 hSyncCount, HW_GPU_STATUS, pending_status;
static unsigned reads;
#define GPUSTATUS_POLLING_THRESHOLD 350
#define SWAP32(value) (value)
static void lightrec_tansition_to_pcsx(struct lightrec_state *s) { (void)s; }
static void lightrec_tansition_from_pcsx(struct lightrec_state *s) { (void)s; }
static u32 original_status(u32 status, u32 cycle, u32 idle, u32 hsync) {
    status |= ((int32_t)(idle - cycle) >> 31) & PSXGPU_nBUSY;
    if (hsync < 240 && (status & PSXGPU_ILACE_BITS) != PSXGPU_ILACE_BITS)
        status |= PSXGPU_LCF & (cycle << 20);
    return status;
}
static u32 psxHwRead32(u32 address) {
    reads++;
    if (address != 0x1f801814) return 0x1234;
    HW_GPU_STATUS = pending_status; /* first read also flushes pending work */
    return original_status(HW_GPU_STATUS, psxRegs.cycle, psxRegs.gpuIdleAfter, hSyncCount);
}
static unsigned skipped_bursts;
static u32 counted_constant_poll_end(u32 status, u32 cycle, u32 interrupt,
                                    u32 stride, unsigned hsync) {
    u32 end = gpuConstantPollEnd(status, cycle, interrupt, stride, hsync);
    if (end != cycle) skipped_bursts++;
    return end;
}
#define gpuConstantPollEnd counted_constant_poll_end
/* Production polling and reference functions are inserted here. */
#undef gpuConstantPollEnd
static u32 random_word(void) {
    static u32 value = 0x951f238a;
    value ^= value << 13; value ^= value >> 17; value ^= value << 5;
    return value;
}
int main(void) {
    struct lightrec_state state = {0};
    const u32 constant = PSXGPU_nBUSY | PSXGPU_LCF;
    assert(gpuConstantPollEnd(constant, 100, 1000, 7, 0) == 1003);
    assert(gpuConstantPollEnd(constant, 100, 1000, 10, 0) == 1000);
    assert(gpuConstantPollEnd(constant, UINT32_MAX - 100,
                             UINT32_MAX, 7, 0) == UINT32_MAX - 100);
    assert(gpuConstantPollEnd(constant, 0, UINT32_MAX, 7, 0) == 0);
    assert(gpuConstantPollEnd(constant, 100, 1000, 0, 0) == 100);
    assert(gpuConstantPollEnd(PSXGPU_nBUSY, 100, 1000, 7, 0) == 100);
    assert(gpuConstantPollEnd(PSXGPU_LCF, 100, 1000, 7, 0) == 100);
    assert(gpuConstantPollEnd(PSXGPU_nBUSY, 100, 1000, 7, 240) == 1003);
    unsigned saved_reads = 0;
    for (unsigned test = 0; test < 10000; test++) {
        u32 base = random_word(), stride = 1 + random_word() % 500;
        if (test % 8 == 0) base = UINT32_MAX - test % 1000;
        if (test % 8 == 1) base = 2040; /* line-bit transition */
        u32 status = random_word(), hsync = test % 300;
        u32 idle = base + (random_word() % 5000);
        if (test % 8 == 2) idle = base + stride * 2; /* busy equality boundary */
        u32 address = test % 17 ? 0x1f801814 : 0x1f801100;
        for (unsigned step = 0; step < 3; step++) {
            u32 start = base + step * stride;
            hSyncCount = hsync;
            psxRegs.gpuIdleAfter = idle;
            psxRegs.next_interupt = start + 5000;
            pending_status = status;
            HW_GPU_STATUS = ~status;
            psxRegs.cycle = start;
            reads = 0;
            u32 expected = hw_read_word_ref(&state, 0, 0, address);
            u32 end = psxRegs.cycle, reference_reads = reads;
            HW_GPU_STATUS = ~status;
            psxRegs.cycle = start;
            reads = 0;
            u32 actual = hw_read_word(&state, 0, 0, address);
            assert(actual == expected && psxRegs.cycle == end);
            assert(reads <= reference_reads);
            saved_reads += reference_reads - reads;
            assert(gpuStatusAtCycle(status, start, idle, hsync) ==
                   original_status(status, start, idle, hsync));
        }
    }
    assert(saved_reads > 10000);
    assert(skipped_bursts > 100);
    return 0;
}
