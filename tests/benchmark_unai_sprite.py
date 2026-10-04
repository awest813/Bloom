#!/usr/bin/env python3
"""Emit a SH-4/host experiment comparing Unai's sprite loop with two-way unrolling.

No renderer source is changed. This measures raw sprite kernels, not game FPS.
Compile emitted C++ with -O3; Dreamcast builds print results to the framebuffer.
"""
import argparse
from pathlib import Path
from test_regressions import ROOT, extract_function


def source(matrix=False, production=False):
    inner = (ROOT / "deps/pcsx_rearmed/plugins/gpu_unai/gpu_inner.h").read_text()
    function = "template<int CF>\n" + extract_function(inner, "gpuSpriteDriverFn")
    fast_begin = function.index("\t  // SH-4 raw 16-bit path;")
    fast_end = function.index("#endif\n", fast_begin) + len("#endif\n")
    original = function[:fast_begin] + function[fast_end:]
    marker = "\t  do\n\t  {"
    assert function.count(marker) == 1
    reference = original.replace("gpuSpriteDriverFn", "reference_sprite")
    helper = ""
    if production:
        helper = "template<bool MaskSet>\n" + extract_function(inner, "gpuSpriteSpan16Raw")
        candidate = function.replace("gpuSpriteDriverFn", "candidate_sprite").replace("#ifdef __sh__", "#if 1")
    else:
        candidate = original.replace("gpuSpriteDriverFn", "candidate_sprite").replace(
            marker, "#pragma GCC unroll 2\n" + marker)
    macros = inner[inner.index("#define  CF_LIGHT"):inner.index("//#ifdef __arm__")]
    return ("#define SPRITE_MATRIX 1\n" if matrix else "") + r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __sh__
#include <kos.h>
#include <arch/timer.h>
static uint64_t now_us() { return timer_us_gettime64(); }
#else
#include <time.h>
static uint64_t now_us() {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}
#endif
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;
typedef uint16_t le16_t;
#define noinline __attribute__((noinline))
#define FRAME_WIDTH 1024
#define MSB_PRESERVED (!CF_DITHER)
static u16 le16_to_u16(le16_t v) { return v; }
static le16_t u16_to_le16(u16 v) { return v; }
// Only raw, unblended instantiations are used in this experiment.
template<int, bool> uint_fast16_t gpuBlending(uint_fast16_t, uint_fast16_t);
uint_fast16_t gpuLightingTXT(uint_fast16_t, u8, u8, u8);
struct gpu_unai_inner_t {
    le16_t *TBA, *CBA;
    u32 u, v, u_msk, v_msk;
    s32 y0, y1;
    u8 r5, g5, b5, pad5, r8, g8, b8, pad8;
    uint64_t gCol, gInc;
    u16 PixelData;
    u8 blit_mask, ilace_mask;
};
''' + macros + helper + reference + candidate + r'''
static u16 a[1024 * 512], b[1024 * 512];
static uint32_t rng = 12345;
static uint32_t random_value() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
typedef void (*Sprite)(le16_t*, u32, const u8*, const gpu_unai_inner_t&);
static void check(Sprite reference, Sprite candidate) {
    for (unsigned trial = 0; trial < 80; trial++) {
        for (unsigned i = 0; i < 1024 * 512; i++)
            a[i] = random_value() % 7 ? (u16)random_value() : 0;
        memcpy(b, a, sizeof(a));
        gpu_unai_inner_t inn = {};
        inn.u = random_value() & 511; inn.v = random_value() & 255;
        inn.u_msk = (random_value() & 255) << 10;
        inn.v_msk = (random_value() & 255) << 10;
        inn.y0 = trial & 3; inn.y1 = inn.y0 + 4;
        inn.ilace_mask = trial & 1;
        const unsigned edges[] = {1, 2, 3, 7, 8, 31, 32, 1023};
        unsigned width = trial < 8 ? edges[trial] : 1 + random_value() % 320;
        // Include destination overlap with texture and palette data, preserving
        // the original per-pixel read/write ordering.
        unsigned dst = trial % 3 == 0 ? 16 : (trial % 3 == 1 ? 512 : 300 * 1024);
        inn.CBA = a + 512;
        reference(a + dst, width, (const u8*)a, inn);
        inn.CBA = b + 512;
        candidate(b + dst, width, (const u8*)b, inn);
        if (memcmp(a, b, sizeof(a))) { printf("FAIL trial %u\n", trial); exit(1); }
    }
}
static uint64_t run(Sprite volatile fn, gpu_unai_inner_t &inn, unsigned width) {
    uint64_t start = now_us();
    for (unsigned i = 0; i < 200; i++) fn(a + 300 * 1024, width, (const u8*)a, inn);
    return now_us() - start;
}
template<int CF> static void bench() {
    check(reference_sprite<CF>, candidate_sprite<CF>);
    gpu_unai_inner_t inn = {};
    inn.CBA = a + 512; inn.u_msk = inn.v_msk = 255 << 10; inn.y1 = 64;
    // Warm both paths, then alternate their measurement order.
    run(reference_sprite<CF>, inn, 160); run(candidate_sprite<CF>, inn, 160);
    for (unsigned pass = 0; pass < 4; pass++) {
        uint64_t old_us, new_us;
        if (pass & 1) { new_us = run(candidate_sprite<CF>, inn, 160); old_us = run(reference_sprite<CF>, inn, 160); }
        else { old_us = run(reference_sprite<CF>, inn, 160); new_us = run(candidate_sprite<CF>, inn, 160); }
        printf("CF=%x pass=%u reference=%llu us unroll2=%llu us\n", CF, pass,
               (unsigned long long)old_us, (unsigned long long)new_us);
    }
}
static void matrix_bench() {
    check(reference_sprite<96>, candidate_sprite<96>);
    const unsigned widths[] = {8, 32, 160};
    for (unsigned pattern = 0; pattern < 3; pattern++) {
        for (unsigned i = 0; i < 1024 * 512; i++)
            a[i] = pattern == 2 || (pattern == 1 && (i & 1)) ? 0 : (u16)(1 + i % 65535);
        gpu_unai_inner_t inn = {};
        inn.u_msk = inn.v_msk = 255 << 10; inn.y1 = 64;
        for (unsigned w = 0; w < 3; w++) {
            run(reference_sprite<96>, inn, widths[w]);
            run(candidate_sprite<96>, inn, widths[w]);
            uint64_t old_total = 0, new_total = 0;
            for (unsigned pass = 0; pass < 4; pass++) {
                if (pass & 1) {
                    new_total += run(candidate_sprite<96>, inn, widths[w]);
                    old_total += run(reference_sprite<96>, inn, widths[w]);
                } else {
                    old_total += run(reference_sprite<96>, inn, widths[w]);
                    new_total += run(candidate_sprite<96>, inn, widths[w]);
                }
            }
            printf("P%u W%u old=%llu new=%llu us\n", pattern, widths[w],
                   (unsigned long long)old_total, (unsigned long long)new_total);
        }
    }
}
int main() {
#ifdef __sh__
    dbgio_dev_select("fb");
#endif
#ifdef SPRITE_CHECK_ONLY
    check(reference_sprite<32>, candidate_sprite<32>);
    check(reference_sprite<64>, candidate_sprite<64>);
    check(reference_sprite<96>, candidate_sprite<96>);
#elif defined(SPRITE_MATRIX)
    matrix_bench();
#else
    bench<32>(); bench<64>(); bench<96>();
#endif
    check(reference_sprite<288>, candidate_sprite<288>);
    check(reference_sprite<320>, candidate_sprite<320>);
    check(reference_sprite<352>, candidate_sprite<352>);
    puts("Sprite comparisons passed");
#ifdef __sh__
    for (;;) thd_sleep(1000);
#endif
}
'''


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emit", type=Path, required=True)
    parser.add_argument("--matrix", action="store_true", help="Vary 16-bit sprite width and transparency")
    parser.add_argument("--production", action="store_true", help="Test the integrated SH-4 raw 16-bit path")
    args = parser.parse_args()
    args.emit.write_text(source(args.matrix, args.production))
