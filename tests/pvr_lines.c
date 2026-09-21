#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static struct { int x1, y1, x2, y2; } sw = { 0, 0, 1024, 512 };
struct pixel { int x, y, r, g, b, semi; };
static struct pixel output[1024], expected[1024];
static unsigned int count;
static uint32_t random_state = 12345;

static unsigned int next_random(void) {
    random_state = random_state * 1664525u + 1013904223u;
    return random_state;
}

static void sw_shade(int x, int y, int u, int v, int r, int g, int b,
                     bool textured, bool semi, bool raw, uint16_t clut, uint16_t page) {
    assert(!textured && !raw && !u && !v && !clut && !page);
    assert(count < 1024);
    output[count++] = (struct pixel){ x, y, r, g, b, semi };
}

/* Production functions are inserted here by test_regressions.py. */

/* Direct evaluation of the original interpolation formula, with C's signed
 * division rounding toward zero. Compare every shader call, in order. */
static void reference(int x0, int y0, int r0, int g0, int b0,
                      int x1, int y1, int r1, int g1, int b1, bool semi) {
    int dx = x1 - x0, dy = y1 - y0;
    int ax = abs(dx), ay = abs(dy), steps = ax > ay ? ax : ay;
    if (ax >= 1024 || ay >= 512) return;
    for (int i = 0; i <= steps; i++) {
        int divisor = steps ? steps : 1;
        int x = x0 + dx * i / divisor, y = y0 + dy * i / divisor;
        if (x >= sw.x1 && x < sw.x2 && y >= sw.y1 && y < sw.y2)
            sw_shade(x, y, 0, 0, r0 + (r1 - r0) * i / divisor,
                     g0 + (g1 - g0) * i / divisor, b0 + (b1 - b0) * i / divisor,
                     false, semi, false, 0, 0);
    }
}

static void compare(const int *v, bool semi) {
    count = 0;
    reference(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], semi);
    unsigned int expected_count = count;
    memcpy(expected, output, sizeof(output));
    count = 0;
    sw_line(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], semi);
    assert(count == expected_count);
    for (unsigned int i = 0; i < count; i++) {
        assert(output[i].x == expected[i].x && output[i].y == expected[i].y);
        assert(output[i].r == expected[i].r && output[i].g == expected[i].g);
        assert(output[i].b == expected[i].b && output[i].semi == expected[i].semi);
    }
}

int main(int argc, char **argv) {
#ifdef BLOOM_BENCHMARK
    int lines[256][10];
    for (unsigned int i = 0; i < 256; i++) {
        for (unsigned int j = 0; j < 10; j++) lines[i][j] = next_random() >> 24;
        lines[i][5] += 512;
    }
    bool baseline = argc > 1 && strcmp(argv[1], "reference") == 0;
    unsigned int checksum = 0;
    clock_t start = clock();
    for (unsigned int repeat = 0; repeat < 500; repeat++) {
        for (unsigned int i = 0; i < 256; i++) {
            const int *v = lines[i];
            count = 0;
            if (baseline)
                reference(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], false);
            else
                sw_line(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], false);
            /* Keep every recorded shader component in the timed workload. */
            __asm__ volatile("" : : "m"(output) : "memory");
            checksum += count + output[count - 1].r;
        }
    }
    printf("%.6f seconds; checksum=%u\n", (double)(clock() - start) / CLOCKS_PER_SEC, checksum);
#else
    const int cases[][10] = {
        {0, 0, 0, 255, 80, 1023, 511, 255, 0, 4},
        {1023, 511, 255, 0, 4, 0, 0, 0, 255, 80},
        {10, 10, 5, 90, 255, 10, 10, 240, 12, 0},
        {0, 0, 255, 0, 3, 1024, 0, 0, 255, 250},
        {0, 0, 255, 0, 3, 0, 512, 0, 255, 250},
        {-100, -10, 2, 30, 90, -90, -5, 254, 0, 90},
        {25, 45, 17, 17, 17, 900, 45, 17, 17, 17},
        {13, 510, 255, 128, 0, 13, 0, 0, 128, 255}
    };
    for (unsigned int i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        compare(cases[i], false);
        compare(cases[i], true);
    }
    for (unsigned int i = 0; i < 4000; i++) {
        int v[10];
        for (unsigned int j = 0; j < 10; j++) v[j] = next_random() >> 24;
        v[0] = (int)(next_random() % 1400) - 200;
        v[1] = (int)(next_random() % 700) - 100;
        v[5] = (int)(next_random() % 1400) - 200;
        v[6] = (int)(next_random() % 700) - 100;
        sw.x1 = next_random() % 100; sw.x2 = 1024 - next_random() % 100;
        sw.y1 = next_random() % 100; sw.y2 = 512 - next_random() % 100;
        compare(v, i & 1);
    }
#endif
    return 0;
}
