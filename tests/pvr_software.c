#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define WITH_PVR_SOFTWARE 1
#define PSX_GPU_STATUS_BLANKING (1u << 23)
#define likely(x) (x)
static struct { unsigned status; uint16_t vram[1024 * 512]; } gpu;
static struct {
    unsigned win_mask_x, win_mask_y, win_off_x, win_off_y, gp1;
    bool check_mask, set_mask;
} pvr = { .win_mask_x = 255, .win_mask_y = 255, .gp1 = 2 << 7 };
static struct { int x1, y1, x2, y2, dx, dy; } sw = {0, 0, 1024, 512, 0, 0};
struct sw_vert { int x, y, u, v, r, g, b; };
union PacketBuffer { uint32_t U4[16]; uint16_t U2[32]; uint8_t U1[64]; };
static bool overlap_draw_area(int x0, int y0, int x1, int y1) {
    return x0 < 320 && x1 > 0 && y0 < 240 && y1 > 0;
}
static void pvr_update_caches(int x, int y, int w, int h, bool invalidate_only) {}
static void process_gpu_commands(void);

/* Production functions are inserted here by test_regressions.py. */

static union PacketBuffer queued;
static bool pending;
/* A minimal queue adapter: synchronization and rasterization are production code. */
static void process_gpu_commands(void) {
    if (pending) {
        assert(sw_draw(&queued, queued.U4[0] >> 24));
        pending = false;
    }
}

int main(void) {
    queued = (union PacketBuffer){.U4 = {0x600000ff, 20 | (10u << 16), 8 | (8u << 16)}};
    pending = true;
    assert(gpu.vram[10 * 1024 + 20] == 0);
    renderer_sync();
    /* A CPU read after synchronization sees every visible sprite pixel. */
    for (int y = 10; y < 18; y++)
        for (int x = 20; x < 28; x++) assert(gpu.vram[y * 1024 + x] == 31);
    assert(!gpu.vram[10 * 1024 + 28]);

    /* Sample those just-rendered pixels as a raw 16-bit texture. */
    union PacketBuffer texture = {.U4 = {0x65000000, 100 | (40u << 16),
                                         20 | (10u << 8), 8 | (8u << 16)}};
    assert(sw_draw(&texture, 0x65));
    for (int y = 40; y < 48; y++)
        for (int x = 100; x < 108; x++) assert(gpu.vram[y * 1024 + x] == 31);

    /* CPU VRAM copies are immediately available to texture sampling too. */
    for (int y = 0; y < 8; y++)
        memcpy(&gpu.vram[(60 + y) * 1024 + 120],
               &gpu.vram[(40 + y) * 1024 + 100], 8 * sizeof(uint16_t));
    texture.U4[1] = 180 | (80u << 16);
    texture.U4[2] = 120 | (60u << 8);
    assert(sw_draw(&texture, 0x65));
    assert(gpu.vram[80 * 1024 + 180] == 31);
    assert(gpu.vram[87 * 1024 + 187] == 31);

    /* Masked writes operate on the same memory. */
    pvr.set_mask = true;
    queued.U4[0] = 0x6000ff00;
    pending = true;
    renderer_sync();
    assert(gpu.vram[10 * 1024 + 20] == (0x8000 | (31 << 5)));
    pvr.check_mask = true;
    queued.U4[0] = 0x60ff0000;
    pending = true;
    renderer_sync();
    assert(gpu.vram[10 * 1024 + 20] == (0x8000 | (31 << 5)));
    renderer_sync(); /* An empty queue must not change memory. */
    return 0;
}
