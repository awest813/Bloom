#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TEX_WIDTH 1024
#define TEX_HEIGHT 512
static uint32_t output[TEX_WIDTH * TEX_HEIGHT / 2];
/* A real SQ lock returns an alias, not the incoming texture address. */
static uint32_t texture_address;
static uint32_t *pvram_sq = &texture_address;
static unsigned int locks, unlocks, flushes;
static uint32_t *sq_lock(uint32_t *dest) {
    assert(dest == pvram_sq && locks == unlocks);
    locks++;
    return output;
}
static void sq_unlock(void) { assert(locks == unlocks + 1); unlocks++; }
static void sq_flush(uint32_t *line) {
    assert(locks == unlocks + 1);
    assert(line >= output && line + 8 <= output + sizeof(output) / sizeof(output[0]));
    flushes++;
}

/* Production functions are inserted here by test_regressions.py. */

static void check(const uint8_t *vram, int offset, int w, int h, bool rgb24) {
    const unsigned int size = TEX_WIDTH * TEX_HEIGHT * 2;
    const unsigned int bpp = rgb24 ? 3 : 2;
    memset(output, 0xa5, sizeof(output));
    locks = unlocks = flushes = 0;
    copy_scanout(vram, offset, w, h, rgb24);
    if (!vram || offset < 0 || (unsigned int)offset >= size || w <= 0 || w > 1024 || h <= 0 || h > 512) {
        assert(!locks && !unlocks && !flushes);
        return;
    }
    assert(locks == 1 && unlocks == locks);
    assert(flushes == (unsigned int)((w + 15) / 16 * h));
    for (int y = 0; y < h; y++) for (int x = 0; x < TEX_WIDTH; x++) {
        uint16_t expected = 0xa5a5;
        if (x < ((w + 15) & ~15)) {
            expected = 0;
            unsigned int pos = offset + y * 2048 + x * bpp;
            if (x < w && pos + bpp <= size) {
                if (rgb24) expected = ((vram[pos] / 8) << 11)
                    | ((vram[pos + 1] / 4) << 5) | (vram[pos + 2] / 8);
                else {
                    unsigned int c = vram[pos] + 256u * vram[pos + 1];
                    expected = ((c % 32) << 10) | (c & 992) | ((c / 1024) % 32);
                }
            }
        }
        uint32_t word = output[y * (TEX_WIDTH / 2) + x / 2];
        assert((uint16_t)(word >> ((x & 1) * 16)) == expected);
    }
    if (h < TEX_HEIGHT) assert(output[h * (TEX_WIDTH / 2)] == 0xa5a5a5a5);
}

int main(void) {
    /* Exactly one VRAM allocation: ASan catches reads past its final byte. */
    const int size = TEX_WIDTH * TEX_HEIGHT * 2;
    uint8_t *vram = malloc(size);
    assert(vram);
    for (int i = 0; i < size; i++) vram[i] = (uint8_t)(i * 37 + i / 2048);
    const int widths[] = {1, 2, 3, 15, 16, 17, 31, 32, 33, 256, 320, 368, 384, 512, 640, 1024};
    const int offsets[] = {0, 1, 2, 3, 2015, 2046, size - 2048, size - 49, size - 3, size - 1};
    for (int mode = 0; mode < 2; mode++) {
        for (unsigned int w = 0; w < sizeof(widths) / sizeof(widths[0]); w++)
            for (unsigned int o = 0; o < sizeof(offsets) / sizeof(offsets[0]); o++)
                check(vram, offsets[o], widths[w], 3, mode);
        check(vram, 0, 640, 512, mode);
        check(vram, 0, 0, 1, mode);
        check(vram, 0, 1025, 1, mode);
        check(vram, 0, 1, 513, mode);
        check(vram, -1, 16, 1, mode);
        check(vram, size, 16, 1, mode);
        check(NULL, 0, 16, 1, mode);
    }
    free(vram);
    return 0;
}
