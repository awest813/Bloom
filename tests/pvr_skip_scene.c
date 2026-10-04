#include <assert.h>
#include <stdint.h>
#include <string.h>
#define WITH_PVR_SOFTWARE 0
#define PSX_GPU_STATUS_RGB24 (1u << 21)
static struct {
    uint32_t status;
    struct { int set, active, allow, frame_ready; } frameskip;
    struct { int x, y, hres, vres; } screen;
} gpu;
static struct {
    int new_frame, clip_test;
    int16_t draw_x1, draw_y1, draw_x2, draw_y2;
    int16_t start_x, start_y, view_x, view_y;
    int16_t draw_dx, draw_dy, draw_offt_x, draw_offt_y;
} pvr;
/* Insert production lace notification here. */
int main(void) {
    gpu.screen.hres = 320; gpu.screen.vres = 240;
    gpu.screen.x = 4; gpu.screen.y = 3;
    gpu.frameskip.set = -2; gpu.frameskip.active = 1;
    gpu.frameskip.allow = 1; gpu.frameskip.frame_ready = 1;
    pvr.new_frame = 1; pvr.view_y = 240;
    pvr.draw_x2 = 320; pvr.draw_y2 = 240;
    pvr.draw_dx = 160; pvr.draw_dy = 120;
    renderer_notify_update_lace(0);
    assert(!gpu.frameskip.frame_ready && pvr.start_y == 240);
    assert(pvr.draw_y1 == -240 && pvr.draw_y2 == 0 && pvr.clip_test);
    assert(pvr.draw_offt_x == 164 && pvr.draw_offt_y == -117);
    renderer_notify_update_lace(0); /* Repeated guest vblank is idempotent. */
    assert(pvr.draw_y1 == -240 && pvr.draw_y2 == 0);
    pvr.view_y = 0;
    renderer_notify_update_lace(0);
    assert(pvr.start_y == 0 && pvr.draw_y1 == 0 && pvr.draw_y2 == 240);
    assert(!pvr.clip_test && pvr.draw_offt_y == 123);
    /* Keep submitted geometry and its coordinate system together. */
    pvr.new_frame = 0; pvr.view_y = 240;
    renderer_notify_update_lace(0);
    assert(gpu.frameskip.frame_ready && pvr.start_y == 0);
    pvr.new_frame = 1; gpu.frameskip.allow = 0;
    gpu.frameskip.frame_ready = 0;
    renderer_notify_update_lace(0);
    assert(gpu.frameskip.frame_ready && pvr.start_y == 0);
    gpu.frameskip.allow = 1; gpu.frameskip.active = 0;
    renderer_notify_update_lace(0);
    assert(gpu.frameskip.frame_ready && pvr.start_y == 0);
    gpu.frameskip.active = 1;
    renderer_notify_update_lace(1);
    assert(gpu.frameskip.frame_ready && pvr.start_y == 0);
    gpu.frameskip.set = 0;
    renderer_notify_update_lace(0);
    assert(gpu.frameskip.frame_ready && pvr.start_y == 0);
    gpu.frameskip.set = -2; gpu.status = PSX_GPU_STATUS_RGB24;
    renderer_notify_update_lace(0);
    assert(gpu.frameskip.frame_ready && pvr.start_y == 0);
    return 0;
}
