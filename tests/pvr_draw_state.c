#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
enum texture_bpp { TEXTURE_4BPP, TEXTURE_8BPP, TEXTURE_16BPP };
enum blending_mode { BLENDING_MODE_NONE = 4 };
static struct {
    uint32_t gp1;
    unsigned int page_x, page_y, win_mask_x, win_mask_y, win_off_x, win_off_y;
    enum blending_mode blending_mode;
    struct { enum texture_bpp bpp; unsigned mask_x, mask_y, offt_x, offt_y; } settings;
} pvr;
static struct { int x1, y1, x2, y2, dx, dy; } sw;
static uint32_t window;
static unsigned int pixels;
static uint8_t seen[29][41];
static int16_t x_to_xoffset(int16_t x) { return x; }
static int16_t y_to_yoffset(int16_t y) { return y; }
static unsigned int mapped(unsigned int uv, unsigned int mask, unsigned int off) {
    return ((uv & 255) & ~(mask * 8)) | ((off & mask) * 8);
}
static void draw_sprite_quad(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                             uint16_t u0, uint16_t v0, uint16_t u1, uint16_t v1,
                             uint32_t color, uint16_t flags, enum blending_mode mode, uint16_t clut) {
    assert(x1 > x0 && y1 > y0 && x0 >= 0 && y0 >= 0 && x1 <= 41 && y1 <= 29);
    assert(u1 - u0 == x1 - x0 && v1 - v0 == y1 - y0);
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
        assert(!seen[y][x]++);
        assert((unsigned int)(u0 + x - x0) == mapped(250 + x, window & 31, (window >> 10) & 31));
        assert((unsigned int)(v0 + y - y0) == mapped(249 + y, (window >> 5) & 31, (window >> 15) & 31));
        pixels++;
    }
}

/* Production functions are inserted here by test_regressions.py. */

int main(void) {
    for (unsigned int mask = 0; mask < 32; mask++) for (unsigned int off = 0; off < 32; off++) {
        window = mask | (mask << 5) | (off << 10) | (off << 15);
        texwin_set(window);
        for (unsigned int uv = 0; uv < 256; uv++) {
            assert(((uv & pvr.win_mask_x) | pvr.win_off_x) == mapped(uv, mask, off));
            assert(((uv & pvr.win_mask_y) | pvr.win_off_y) == mapped(uv, mask, off));
            unsigned int span = texwin_span(pvr.win_mask_x);
            unsigned int end = uv + span - (uv & (span - 1));
            assert(texwin_range_fits(uv, end, uv, end));
            for (unsigned int i = uv; i < end; i++)
                assert(mapped(i, mask, off) == mapped(uv, mask, off) + i - uv);
        }
        pixels = 0; memset(seen, 0, sizeof(seen));
        draw_textured_sprite(0, 0, 41, 29, 250, 249, 0, 0, BLENDING_MODE_NONE, 0);
        assert(pixels == 41 * 29);
    }
    /* Empty drawing areas must stay empty after restoring saved registers. */
    uint32_t regs[7] = {0};
    regs[3] = 100 | (80 << 10); regs[4] = 99 | (79 << 10);
    regs[5] = 0x7ff | (0x400 << 11);
    sw_sync_ecmds(regs);
    assert(sw.x1 == sw.x2 && sw.y1 == sw.y2 && sw.dx == -1 && sw.dy == -1024);
    regs[4] = 20 | (10 << 10); sw_sync_ecmds(regs);
    assert(sw.x2 < sw.x1 && sw.y2 < sw.y1);
    for (unsigned int word = 0; word < 65536; word++)
        assert(psx_coord(word) == (int)((word + 1024) & 2047) - 1024);
    pvr_set_texture_page(0x600, 0x7ff);
    pvr_set_texture_page(0x1ff, 0x1ff);
    assert(pvr.gp1 == 0x7ff && pvr.page_x == 15 && pvr.page_y == 1);
    assert((unsigned)pvr.blending_mode == 3 && (unsigned)pvr.settings.bpp == 3);
    pvr_set_texture_page(0, 0x1ff);
    assert(pvr.gp1 == 0x600 && !pvr.page_x && !pvr.page_y);
    return 0;
}
