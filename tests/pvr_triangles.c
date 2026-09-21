#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static struct { uint16_t vram[512 * 1024]; } gpu;
static struct {
    unsigned int win_mask_x, win_mask_y, win_off_x, win_off_y, gp1;
    bool check_mask, set_mask;
} pvr = { .win_mask_x = 255, .win_mask_y = 255 };
static struct { int x1, y1, x2, y2; } sw = { 0, 0, 1024, 512 };
struct sw_vert { int x, y, u, v, r, g, b; };
static uint16_t expected[512 * 1024];
static uint32_t random_state = 1;

static unsigned int next_random(void) {
    random_state = random_state * 1664525u + 1013904223u;
    return random_state;
}

/* Production functions are inserted here by test_regressions.py. */

static int64_t edge(const struct sw_vert *a, const struct sw_vert *b, int x, int y) {
    return (int64_t)(b->x - a->x) * (y - a->y)
         - (int64_t)(b->y - a->y) * (x - a->x);
}

/* Independent scanline oracle: intersect non-horizontal edges with each row,
 * then cover the half-open span between rational left/right intersections. */
static void reference(const struct sw_vert *a, const struct sw_vert *b,
                      const struct sw_vert *c, bool semi) {
    int64_t area = edge(a, b, c->x, c->y);
    int maxx = a->x > b->x ? a->x : b->x;
    int maxy = a->y > b->y ? a->y : b->y;
    maxx = maxx > c->x ? maxx : c->x;
    maxy = maxy > c->y ? maxy : c->y;
    if (!area) return;
    for (int y = sw.y1; y < sw.y2 && y < maxy; y++) {
        const struct sw_vert *verts[] = {a, b, c, a};
        int64_t numerator[2], denominator[2];
        int count = 0;
        for (int e = 0; e < 3; e++) {
            const struct sw_vert *lo = verts[e], *hi = verts[e + 1];
            if (lo->y > hi->y) { const struct sw_vert *tmp = lo; lo = hi; hi = tmp; }
            if (y < lo->y || y >= hi->y) continue;
            assert(count < 2);
            denominator[count] = hi->y - lo->y;
            numerator[count] = (int64_t)lo->x * denominator[count]
                             + (int64_t)(y - lo->y) * (hi->x - lo->x);
            count++;
        }
        if (count != 2) continue;
        int left = numerator[0] * denominator[1] <= numerator[1] * denominator[0] ? 0 : 1;
        int right = 1 - left;
        for (int x = sw.x1; x < sw.x2 && x < maxx; x++) {
            if ((int64_t)x * denominator[left] < numerator[left]
                || (int64_t)x * denominator[right] >= numerator[right]) continue;
            uint16_t old = expected[y * 1024 + x], value = 0;
            if (pvr.check_mask && (old & 0x8000)) continue;
            const int components[] = { a->r / 8, a->g / 8, a->b / 8 };
            for (int ch = 0; ch < 3; ch++) {
                int src = components[ch], dst = (old >> (ch * 5)) & 31;
                if (semi) {
                    switch ((pvr.gp1 >> 5) & 3) {
                    case 0: src = (dst + src) / 2; break;
                    case 1: src += dst; break;
                    case 2: src = dst - src; break;
                    case 3: src = dst + src / 4; break;
                    }
                }
                if (src < 0) src = 0;
                if (src > 31) src = 31;
                value |= src << (ch * 5);
            }
            expected[y * 1024 + x] = value | (pvr.set_mask ? 0x8000 : 0);
        }
    }
}

static void check_shared_edges(void) {
    const unsigned order[6][3] = {{0,1,2}, {0,2,1}, {1,0,2}, {1,2,0}, {2,0,1}, {2,1,0}};
    const unsigned split[2][2][3] = {{{0,1,2}, {1,2,3}}, {{0,1,3}, {0,2,3}}};
    const uint16_t result[] = {12, 24, 8, 18};
    pvr.check_mask = pvr.set_mask = false;
    for (int path = 0; path < 3; path++)
    for (int diagonal = 0; diagonal < 2; diagonal++)
    for (int permutation = 0; permutation < 6; permutation++)
    for (int clipped = 0; clipped < 2; clipped++)
    for (unsigned mode = 0; mode < 4; mode++) {
        struct sw_vert corners[4] = {
            {.x=2, .y=2, .r=64}, {.x=14, .y=2, .r=64},
            {.x=2, .y=14, .r=64}, {.x=14, .y=14, .r=64}
        };
        sw.x1 = sw.y1 = clipped ? 5 : 0;
        sw.x2 = sw.y2 = clipped ? 11 : 16;
        pvr.gp1 = mode << 5;
        for (unsigned i = 0; i < 512 * 1024; i++) gpu.vram[i] = 16;
        gpu.vram[64] = 0x8008;
        for (int triangle = 0; triangle < 2; triangle++) {
            struct sw_vert v[3];
            for (int i = 0; i < 3; i++) {
                v[i] = corners[split[diagonal][triangle][order[permutation][i]]];
                /* Different colors force interpolation while remaining in
                 * the same 5-bit output bucket at every covered pixel. */
                if (path == 1) v[i].r += i;
            }
            sw_triangle(&v[0], &v[1], &v[2], path == 2, true, path == 2,
                        0, 0x101 | (mode << 5));
        }
        for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            bool covered = x >= 2 && x < 14 && y >= 2 && y < 14
                        && x >= sw.x1 && x < sw.x2 && y >= sw.y1 && y < sw.y2;
            uint16_t want = covered ? result[mode] | (path == 2 ? 0x8000 : 0) : 16;
            assert(gpu.vram[y * 1024 + x] == want);
        }
    }
}

int main(void) {
#ifdef BLOOM_BENCHMARK
    struct sw_vert a = { .x = 0, .y = 0, .r = 255, .g = 80, .b = 24 };
    struct sw_vert b = a, c = a;
    b.x = 320; c.y = 240;
    clock_t start = clock();
    for (int i = 0; i < 5000; i++) {
        /* Change color on each draw and expose VRAM to prevent dead-store
         * elimination from reducing the workload to its final triangle. */
        a.r = b.r = c.r = i & 255;
        sw_triangle(&a, &b, &c, false, false, false, 0, 0);
        __asm__ volatile("" : : "m"(gpu.vram) : "memory");
    }
    double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;
    uint32_t checksum = 0;
    for (unsigned int i = 0; i < 512 * 1024; i++)
        checksum = checksum * 31u + gpu.vram[i];
    printf("%.6f seconds; checksum=%u\n", elapsed, checksum);
#else
    check_shared_edges();
    for (unsigned int trial = 0; trial < 240; trial++) {
        struct sw_vert v[3];
        int r = next_random() >> 24, g = next_random() >> 24, b = next_random() >> 24;
        for (int i = 0; i < 3; i++) {
            v[i] = (struct sw_vert){
                .x = (int)((next_random() >> 16) % 160) - 32,
                .y = (int)((next_random() >> 16) % 160) - 32,
                .r = r, .g = g, .b = b
            };
        }
        if (trial % 10 == 0) v[2] = v[1]; /* Degenerate triangles. */
        sw.x1 = trial % 17; sw.y1 = trial % 13;
        sw.x2 = 96; sw.y2 = 96;
        /* Also exercise bottom/right VRAM boundaries. */
        if (trial & 1) {
            sw.x1 += 928; sw.x2 += 928;
            sw.y1 += 416; sw.y2 += 416;
            for (int i = 0; i < 3; i++) { v[i].x += 928; v[i].y += 416; }
        }
        pvr.gp1 = (trial % 4) << 5;
        pvr.check_mask = (trial >> 2) & 1;
        pvr.set_mask = (trial >> 3) & 1;
        bool semi = (trial >> 4) & 1;
        for (unsigned int i = 0; i < 512 * 1024; i++)
            gpu.vram[i] = expected[i] = (uint16_t)next_random();
        reference(&v[0], &v[1], &v[2], semi);
        sw_triangle(&v[0], &v[1], &v[2], false, semi, false, 0, 0);
        assert(memcmp(gpu.vram, expected, sizeof(expected)) == 0);
    }
    /* A negative color gradient must use the general path without signed-shift UB. */
    sw.x1 = sw.y1 = 0; sw.x2 = sw.y2 = 8;
    pvr.check_mask = pvr.set_mask = false;
    memset(gpu.vram, 0, sizeof(gpu.vram));
    struct sw_vert a = { .r = 248 }, b = { .x = 8 }, c = { .y = 8, .r = 248 };
    sw_triangle(&a, &b, &c, false, false, false, 0, 0);
    assert(gpu.vram[0] == 31 && gpu.vram[4] == 15);
    /* Raw textures still sample VRAM, even with identical vertex colors. */
    a.r = b.r = c.r = 128;
    a.u = b.u = c.u = 2; a.v = b.v = c.v = 2;
    gpu.vram[2 * 1024 + 64 + 2] = 0x1234;
    sw_triangle(&a, &b, &c, true, false, true, 0, 0x101);
    assert(gpu.vram[0] == 0x1234);
    /* Textured primitives use their own blend attribute, not an earlier E1. */
    const uint16_t blended[] = {13, 26, 14, 21};
    gpu.vram[2 * 1024 + 64 + 2] = 0x8006;
    for (unsigned int mode = 0; mode < 4; mode++) {
        pvr.gp1 = ((mode + 1) & 3) << 5;
        gpu.vram[0] = 20;
        sw_shade(0, 0, 2, 2, 128, 128, 128, true, true, true, 0, 0x101 | (mode << 5));
        assert(gpu.vram[0] == (0x8000 | blended[mode]));
    }
#endif
    return 0;
}
