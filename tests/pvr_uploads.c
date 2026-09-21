#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TEXTURE_16BPP 2
#define BITLL(n) (UINT64_C(1) << (n))
typedef void *pvr_ptr_t;
struct texture_vq { uint32_t frame[8]; };
struct texture_page {
    struct { int bpp; } settings;
    union { pvr_ptr_t tex; struct texture_vq *vq; };
    uint64_t block_mask;
};
static unsigned int loaded[64], loads, locks, unlocks;
static pvr_ptr_t expected_addr;
static unsigned int expected_page;
#if WITH_PERF_LOG
static struct {
    unsigned int uploads, blocks, triangles, flat_triangles, lines, rectangles;
} perf;
#endif

static uint32_t *sq_lock(pvr_ptr_t addr) {
    assert(addr == expected_addr);
    locks++;
    return addr;
}
static void sq_unlock(void) { unlocks++; }
static void load_block(struct texture_page *page, unsigned int offset,
                       unsigned int x, unsigned int y, uint32_t *sq) {
    assert(offset == expected_page && sq == expected_addr);
    assert(x < 4 && y < 16 && loads < 64);
    loaded[loads++] = y * 4 + x;
}

/* Production functions are inserted here by test_regressions.py. */

static void reference(struct texture_page *page, unsigned int page_offset, uint64_t mask) {
    uint32_t *sq = sq_lock(page->settings.bpp == TEXTURE_16BPP ? page->tex : page->vq->frame);
    for (unsigned int i = 0; i < 64; i++) {
        if (mask & BITLL(i)) {
            load_block(page, page_offset, i % 4, i / 4, sq);
            page->block_mask |= BITLL(i);
        }
    }
    sq_unlock();
}

static void check(uint64_t mask, int bpp) {
    struct texture_vq vq;
    uint32_t pixels[8];
    const uint64_t previous = UINT64_C(0x8102040810204081);
    struct texture_page page = { .settings.bpp = bpp, .block_mask = previous };
    if (bpp == TEXTURE_16BPP) { page.tex = pixels; expected_addr = pixels; }
    else { page.vq = &vq; expected_addr = vq.frame; }
    expected_page = 17;
    loads = locks = unlocks = 0;
#if WITH_PERF_LOG
    memset(&perf, 0, sizeof(perf));
#endif
    update_texture(&page, expected_page, mask);
    assert(locks == 1 && unlocks == 1);
    unsigned int count = 0;
    for (unsigned int bit = 0; bit < 64; bit++) {
        if ((mask >> bit) & 1) {
            assert(count < loads && loaded[count] == bit);
            count++;
        }
    }
    assert(count == loads && page.block_mask == (previous | mask));
#if WITH_PERF_LOG
    assert(perf.uploads == 1 && perf.blocks == count);
#endif
}

int main(int argc, char **argv) {
#ifdef BLOOM_BENCHMARK
    struct texture_vq vq;
    struct texture_page page = { .settings.bpp = 0, .vq = &vq };
    expected_addr = vq.frame;
    bool baseline = argc > 1 && strcmp(argv[1], "reference") == 0;
    int density = argc > 2 ? atoi(argv[2]) : 1;
    unsigned int checksum = 0;
    clock_t start = clock();
    for (unsigned int i = 0; i < 5000000; i++) {
        uint64_t mask = BITLL(i % 64);
        if (density == 4) mask |= BITLL((i + 17) % 64) | BITLL((i + 34) % 64) | BITLL((i + 51) % 64);
        if (density == 64) mask = UINT64_MAX;
        loads = 0;
        page.block_mask = 0;
        if (baseline) reference(&page, 0, mask);
        else update_texture(&page, 0, mask);
        /* Preserve the complete block trace, not just its final entry. */
        __asm__ volatile("" : : "m"(loaded), "m"(page) : "memory");
        checksum += loads + loaded[loads - 1] + (uint32_t)page.block_mask;
    }
    printf("%.6f seconds; checksum=%u\n", (double)(clock() - start) / CLOCKS_PER_SEC, checksum);
#else
    for (int bpp = 0; bpp <= TEXTURE_16BPP; bpp++) {
        check(0, bpp);
        check(UINT64_MAX, bpp);
        check(UINT64_C(0xffffffff), bpp);
        check(UINT64_C(0xffffffff00000000), bpp);
        for (unsigned int a = 0; a < 64; a++) {
            check(BITLL(a), bpp);
            for (unsigned int b = 0; b < 64; b++) check(BITLL(a) | BITLL(b), bpp);
        }
        uint64_t random = 7;
        for (unsigned int i = 0; i < 4096; i++) {
            random = random * UINT64_C(6364136223846793005) + 1;
            check(random, bpp);
        }
    }
#if WITH_PERF_LOG
    perf.triangles = 4; perf.flat_triangles = 3; perf.lines = 2; perf.rectangles = 1;
    pvr_perf_report();
    assert(!perf.uploads && !perf.blocks && !perf.triangles && !perf.flat_triangles);
    assert(!perf.lines && !perf.rectangles);
#endif
#endif
    return 0;
}
