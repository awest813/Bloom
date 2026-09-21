#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define WITH_PVR_SOFTWARE 0
#define PSX_GPU_STATUS_BLANKING (1u << 23)
#define PVR_OPT_HYBRID() 1
#define PVR_OPT_CLIP() 1
#define likely(x) (x)
#define TEXWIN_SPLIT_MAX 64
static struct { unsigned int status; } gpu;
static struct { uint16_t gp1; } pvr;
static struct { int x1, y1, x2, y2, dx, dy; } sw = {0, 0, 1024, 512, 0, 0};
union PacketBuffer { uint32_t U4[16]; uint16_t U2[32]; uint8_t U1[64]; };
struct sw_vert { int x, y, u, v, r, g, b; };
struct poly { int unused; };
static unsigned int pixels, uploads, hardware, discarded;
static uint8_t written[512][1024];
static bool overlap_draw_area(int x0, int y0, int x1, int y1) {
    return x0 < 320 && x1 > 0 && y0 < 240 && y1 > 0;
}
static void sw_shade(int x, int y, int u, int v, int r, int g, int b,
                     bool textured, bool semi, bool raw, uint16_t clut, uint16_t page) {
    assert(x >= 0 && x < 1024 && y >= 0 && y < 512);
    written[y][x] = 1; pixels++;
}
static void sw_triangle(struct sw_vert *a, struct sw_vert *b, struct sw_vert *c,
                        bool t, bool s, bool r, uint16_t clut, uint16_t page) {
    assert(!"this fixture uses sprites and lines");
}
static void pvr_update_caches(int x, int y, int w, int h, bool invalidate_only) {
    assert(invalidate_only && w > 0 && h > 0); uploads++;
}
static void poly_discard(struct poly *poly) { discarded++; }
static void process_poly_inner(struct poly *poly, bool scissor, int budget) { hardware++; }

/* Production functions are inserted here by test_regressions.py. */

int main(void) {
    assert(psx_coord(0) == 0 && psx_coord(1023) == 1023);
    assert(psx_coord(1024) == -1024 && psx_coord(2047) == -1);
    assert(psx_coord(0xffff0000u | 1023) == 1023);
    union PacketBuffer sprite = {.U4 = {0x600000ff, 20 | (10u << 16), 8 | (8u << 16)}};
    union PacketBuffer line = {.U4 = {0x400000ff, 20 | (10u << 16), 23 | (10u << 16)}};
    struct poly poly = {0};
    /* Visible geometry stays on PVR while scanout is enabled. */
    assert(!sw_draw(&sprite, 0x60));
    assert(!sw_draw_lines(&line, 0x40, 2));
    assert(pixels == 0);
    process_poly(&poly, false);
    assert(hardware == 1);

    /* Disabled scanout must still write VRAM, without growing PVR queues. */
    gpu.status = PSX_GPU_STATUS_BLANKING;
    assert(sw_draw(&sprite, 0x60));
    assert(pixels == 64 && written[10][20] && written[17][27]);
    assert(sw_draw_lines(&line, 0x40, 2));
    assert(pixels == 68 && uploads == 2);
    for (int i = 0; i < 10000; i++) process_poly(&poly, false);
    assert(hardware == 1 && discarded == 10000);

    /* Drawing-area clipping remains in effect during blanking. */
    sw.x1 = 22; sw.y1 = 12; sw.x2 = 25; sw.y2 = 15;
    pixels = 0; memset(written, 0, sizeof(written));
    assert(sw_draw(&sprite, 0x60));
    assert(pixels == 9 && written[12][22] && written[14][24]);
    assert(!written[11][22] && !written[15][24]);

    sw.x1 = sw.y1 = 0; sw.x2 = 1024; sw.y2 = 512;
    sprite.U4[1] = 0x7ff | (0x7ffu << 16); /* (-1, -1) */
    pixels = 0;
    assert(sw_draw(&sprite, 0x60) && pixels == 49);
    sprite.U4[1] = 1022 | (510u << 16);
    pixels = 0;
    assert(sw_draw(&sprite, 0x60) && pixels == 4);

    /* Re-enabling scanout resumes the normal hardware path. */
    gpu.status = 0;
    sprite.U4[1] = 20 | (10u << 16);
    assert(!sw_draw(&sprite, 0x60));
    process_poly(&poly, false);
    assert(hardware == 2);
    sw.x1 = sw.y1 = 0; sw.x2 = 1024; sw.y2 = 512;
    sprite.U4[1] = 400 | (300u << 16);
    assert(sw_draw(&sprite, 0x60)); /* Off-screen VRAM still uses software. */
    return 0;
}
