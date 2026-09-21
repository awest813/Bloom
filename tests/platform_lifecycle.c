#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define TEX_WIDTH 1024
#define TEX_HEIGHT 512
#define PVR_LIST_OP_POLY 0
#define PVR_TXRFMT_NONTWIDDLED 0
#define PVR_TXRFMT_RGB565 1
#define PVR_TXRFMT_ARGB1555 2
#define PVR_FILTER_NONE 0
#define PVR_CMD_VERTEX 0
#define PVR_CMD_VERTEX_EOL 1
#define PVR_PACK_COLOR(a,r,g,b) 0xffffffffu
typedef struct { int unused; } pvr_poly_cxt_t;
typedef struct { int unused; } pvr_poly_hdr_t;
typedef struct { uint32_t argb, oargb, flags; float x,y,z,u,v; } pvr_vertex_t;
static bool started, frame_was_24bpp, stats_started;
static unsigned frames;
static float screen_fw = 1, screen_fh = 1;
static void *pvram;
static uint8_t scanout_commands[1024];
static void *pvr_set_vertbuf(int list, void *buffer, size_t size) {
    assert(buffer == scanout_commands && size >= 512);
    return NULL;
}
static bool hardware_open, scene_open, list_open, ta_busy, render_busy;
static unsigned copies, allocations, frees, flips;
static void pvr_wait_ready(void) {
    if (ta_busy) { ta_busy = false; render_busy = true; }
}
static void pvr_wait_render_done(void) {
    assert(!ta_busy); /* Waiting only for the older render is insufficient. */
    render_busy = false;
}
static void pvr_scene_begin(void) { assert(!scene_open && !ta_busy); scene_open = true; }
static void pvr_scene_finish(void) {
    assert(scene_open && !list_open);
    scene_open = false; ta_busy = true;
}
static void pvr_list_begin(int list) { assert(scene_open && !list_open); list_open = true; }
static void pvr_list_finish(void) { assert(list_open); list_open = false; }
static void pvr_poly_cxt_txr(pvr_poly_cxt_t *c, int list, int format, int w, int h, void *p, int filter) {
    assert(p && p == pvram);
}
static void pvr_poly_compile(pvr_poly_hdr_t *h, pvr_poly_cxt_t *c) {}
static void pvr_prim(const void *data, size_t size) { assert(scene_open && list_open); }
static void hw_render_start(void) { assert(!hardware_open); hardware_open = true; }
static void hw_render_stop(void) {
    assert(hardware_open);
    hardware_open = false; ta_busy = true;
}
static void dc_alloc_pvram(void) { assert(!pvram); pvram = &allocations; allocations++; }
static void pvr_mem_free(void *p) {
    assert(p && p == pvram && !ta_busy && !render_busy);
    pvram = NULL; frees++;
}
static void invalidate_all_textures(void) {}
static void copy_scanout(const void *p, int off, int w, int h, bool bpp) {
    assert(pvram && !ta_busy && !render_busy); copies++;
}
static void dc_vout_report_stats(void) { flips++; }
static int OpenPlugins(void);

/* Production functions are inserted here by test_regressions.py. */

static bool fail_open;
static int OpenPlugins(void) {
    assert(started);
    if (fail_open) return -1;
    return dc_vout_open();
}

int main(void) {
    uint16_t pixels[1] = {0};
    dc_vout_open(); dc_vout_close();
    assert(!allocations && !hardware_open);
    fail_open = true;
    assert(emu_open_game_plugins() == -1 && !started);
    fail_open = false;
    for (int close_in_24bit = 0; close_in_24bit < 2; close_in_24bit++) {
        started = false;
        assert(emu_open_game_plugins() == 0 && started);
        assert(USE_PVR_RENDERER ? hardware_open : pvram != NULL);
        dc_vout_flip(pixels, 0, 0, 0, 0, 320, 240, 0);
        dc_vout_flip(pixels, 0, 1, 0, 0, 320, 240, 1);
        dc_vout_flip(pixels, 0, 1, 0, 0, 320, 240, 0);
        dc_vout_flip(NULL, 0, 1, 0, 0, 320, 240, 0);
        dc_vout_flip(pixels, 0, 1, 0, 0, 320, 240, 0);
        if (!close_in_24bit) {
            dc_vout_flip(pixels, 0, 0, 0, 0, 320, 240, 1);
            dc_vout_flip(NULL, 0, 0, 0, 0, 320, 240, 0);
        }
        dc_vout_close();
        assert(!hardware_open && !scene_open && !ta_busy && !render_busy && !pvram);
        assert(allocations == frees);
    }
    assert(copies > 0 && flips == 9);
    return 0;
}
