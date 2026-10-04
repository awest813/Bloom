#!/usr/bin/env python3
"""Emit differential/SH-4 timing checks for the neutral polygon lighting path."""
import argparse
from pathlib import Path
from test_regressions import ROOT, extract_function


def source():
    base = ROOT / 'deps/pcsx_rearmed/plugins/gpu_unai'
    inner = (base / 'gpu_inner.h').read_text()
    interface = (base / 'gpulib_if.cpp').read_text()
    macros = inner[inner.index('#define  CF_LIGHT'):inner.index('//#ifdef __arm__')]
    span = 'template<int CF>\n' + extract_function(inner, 'gpuPolySpanFn')
    return r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __sh__
#include <kos.h>
#include <arch/timer.h>
#endif
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;
typedef int32_t s32; typedef uint16_t le16_t;
#define GPU_INLINE static inline
#define noinline __attribute__((noinline))
#define HTOLE32(x) (x)
#define MSB_PRESERVED (!CF_DITHER)
static u16 le16_to_u16(le16_t v) { return v; }
static le16_t u16_to_le16(u16 v) { return v; }
union gcol_t { struct {u16 r, g, b;} c; uint64_t raw; };
struct gpu_unai_inner_t {
    le16_t *TBA, *CBA;
    u32 u, v, u_msk, v_msk;
    s32 u_inc, v_inc;
    u8 r5, g5, b5, r8, g8, b8;
    gcol_t gCol, gInc;
    u16 PixelData; u8 blit_mask;
};
struct gpu_unai_t {
    gpu_unai_inner_t inn;
    u8 LightLUT[1024]; u32 DitherMatrix[64]; le16_t *vram;
};
static gpu_unai_t gpu_unai;
''' + macros + '\n' + '\n'.join((base / h).read_text() for h in (
        'gpu_inner_blend.h', 'gpu_inner_quantization.h', 'gpu_inner_light.h')) + r'''
#define gpuBlending gpuBlendingGeneric
#define gpuLightingTXT gpuLightingTXTGeneric
''' + span + '\n' + extract_function(interface, 'need_lighting') + r'''
// Experimental selector, not currently enabled in the production renderer.
static bool need_flat_polygon_lighting(u32 rgb_raw, u32 dithering) {
    return dithering || need_lighting(rgb_raw);
}
static u16 a[1024*512], b[1024*512];
static u32 rng = 3928147;
static u32 random_value() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
template<int CF> static void check() {
    for (unsigned trial = 0; trial < 16; trial++) {
        for (unsigned i = 0; i < 1024*512; i++)
            a[i] = random_value() % 5 ? (u16)random_value() : 0;
        memcpy(b, a, sizeof(a));
        gpu_unai_inner_t &inn = gpu_unai.inn;
        inn = {};
        inn.r5 = inn.g5 = inn.b5 = 16;
        inn.u_msk = ((random_value() & 255) << 10) | 1023;
        inn.v_msk = ((random_value() & 255) << 10) | 1023;
        inn.u = random_value(); inn.v = random_value();
        inn.u_inc = (s32)(random_value() % 8193) - 4096;
        inn.v_inc = random_value() % 2049;
        // Include feedback through texels and the CLUT, masked destinations,
        // transparent zero, and opaque black (0x8000).
        unsigned dst = trial % 3 == 0 ? 512 : trial % 3 == 1 ? 0 : 300*1024;
        unsigned count = 1 + random_value() % 512;
        inn.TBA = a; inn.CBA = a + 512;
        gpuPolySpanFn<CF | 1>(gpu_unai, a + dst, count);
        inn.TBA = b; inn.CBA = b + 512;
        gpuPolySpanFn<CF>(gpu_unai, b + dst, count);
        if (memcmp(a, b, sizeof(a))) { printf("FAIL flags=%x trial=%u\n", CF, trial); exit(1); }
    }
}
template<int N> struct Matrix {
    static void run() {
        // Three texture depths, four blend modes, mask set/check on and off.
        const int flags = ((N % 3 + 1) << 5) | (((N / 3) % 4) << 3)
            | (((N / 12) % 2) << 2) | (((N / 24) % 2) << 8);
        check<flags>(); check<flags | 2>();
        Matrix<N - 1>::run();
    }
};
template<> struct Matrix<-1> { static void run() {} };
#ifdef __sh__
typedef void (*Span)(const gpu_unai_t&, le16_t*, u32);
static uint64_t time_span(Span volatile fn, unsigned width) {
    uint64_t start = timer_us_gettime64();
    for (unsigned i = 0; i < 10000; i++) fn(gpu_unai, a + 300*1024, width);
    return timer_us_gettime64() - start;
}
template<int CF> static void bench() {
    gpu_unai.inn = {};
    gpu_unai.inn.r5 = gpu_unai.inn.g5 = gpu_unai.inn.b5 = 16;
    gpu_unai.inn.u_msk = gpu_unai.inn.v_msk = 0x3ffff;
    gpu_unai.inn.u_inc = 1024;
    gpu_unai.inn.TBA = a; gpu_unai.inn.CBA = a + 512;
    for (unsigned i = 0; i < 1024*512; i++) a[i] = (u16)(1 + i % 65535);
    const unsigned widths[] = {16u, 64u, 160u};
    for (unsigned width : widths) {
        uint64_t old_us = 0, new_us = 0;
        time_span(gpuPolySpanFn<CF | 1>, width); time_span(gpuPolySpanFn<CF>, width);
        for (unsigned pass = 0; pass < 4; pass++) {
            if (pass & 1) { new_us += time_span(gpuPolySpanFn<CF>, width); old_us += time_span(gpuPolySpanFn<CF | 1>, width); }
            else { old_us += time_span(gpuPolySpanFn<CF | 1>, width); new_us += time_span(gpuPolySpanFn<CF>, width); }
        }
        printf("CF=%x W%u old=%llu new=%llu us\n", CF, width, (unsigned long long)old_us, (unsigned long long)new_us);
    }
}
#endif
int main() {
#ifdef __sh__
    dbgio_dev_select("fb");
#endif
    SetupLightLUT();
    for (u32 rgb = 0; rgb < 0x1000000; rgb++) {
        bool neutral = ((rgb & 255) >> 3) == 16 && (((rgb >> 8) & 255) >> 3) == 16 && ((rgb >> 16) >> 3) == 16;
        if (need_flat_polygon_lighting(rgb, 0) == neutral || !need_flat_polygon_lighting(rgb, 512)) abort();
    }
    Matrix<47>::run();
    puts("Neutral polygon comparisons passed");
#ifdef __sh__
    bench<32>(); bench<64>(); bench<96>();
    for (;;) thd_sleep(1000);
#endif
}
'''


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emit', type=Path, required=True)
    args = parser.parse_args()
    args.emit.write_text(source())
