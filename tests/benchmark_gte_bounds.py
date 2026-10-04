"""Emit a full-register differential test and SH-4 GTE bounds experiment."""
import argparse
from pathlib import Path
from test_regressions import ROOT, extract_function

NAMES = ('RTPS NCLIP OP DPCS INTPL MVMVA NCDS CDP NCDT NCCS CC NCS NCT '
         'SQR DCPL DPCT AVSZ3 AVSZ4 RTPT GPF GPL NCCT').split()


def source(production=False):
    original = (ROOT / 'deps/pcsx_rearmed/libpcsxcore/gte.c').read_text()
    macros = original[original.index('#define VX('):original.index('#ifndef FLAGLESS')]
    # Retain the pre-optimization bounds logic as an independent reference.
    bounds = '''static inline s64 BOUNDS_(psxCP2Regs *regs, s64 n_value,
            s64 n_max, int n_maxflag, s64 n_min, int n_minflag) {
        if (n_value > n_max) gteFLAG |= n_maxflag;
        else if (n_value < n_min) gteFLAG |= n_minflag;
        return n_value;
    }'''
    candidate = bounds.replace('BOUNDS_', 'candidate_bounds').replace(
        'if (n_value > n_max) gteFLAG |= n_maxflag;', '''if (n_max == INT32_MAX && n_min == INT32_MIN) {
        if (n_value != (s32)n_value)
            gteFLAG |= n_value < 0 ? n_minflag : n_maxflag;
    } else if (n_value > n_max) gteFLAG |= n_maxflag;''')
    if production:
        candidate = extract_function(original, 'BOUNDS_').replace('BOUNDS_', 'candidate_bounds')
    limits = original[original.index('#define BOUNDS(n_value'):original.index('#ifndef __arm__')]
    divider = (ROOT / 'deps/pcsx_rearmed/libpcsxcore/gte_divider.c').read_text()
    divider = divider[divider.index('static const u8 table'):]
    functions = '\n'.join(extract_function(original, 'gte' + name) for name in NAMES)
    reference = functions
    for name in NAMES:
        reference = reference.replace('void gte' + name + '(', 'void reference_' + name + '(')
    return r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#ifdef __sh__
#include <kos.h>
#include <arch/timer.h>
#endif
typedef int64_t s64; typedef uint64_t u64;
typedef int32_t s32; typedef uint32_t u32;
typedef int16_t s16; typedef uint16_t u16; typedef uint8_t u8;
typedef union {
    u32 d; s32 sd;
    struct { s16 l, h; } sw;
    struct { u16 l, h; } w;
    struct { u8 l, h, h2, h3; } b;
} pair;
typedef union { u32 r[32]; pair p[32]; } registers;
typedef struct { registers CP2D, CP2C; } psxCP2Regs;
static struct { u32 code; } psxRegs;
_Static_assert(sizeof(psxCP2Regs) == 256, "GTE register layout");
''' + macros + bounds.replace('BOUNDS_', 'reference_bounds') + candidate + '\n' + \
        extract_function(original, 'LIM_') + '\n' + extract_function(original, 'limE_') + \
        '\n' + limits + '\n#define A1U A1\n#define A2U A2\n#define A3U A3\n' + divider + \
        '\n#define BOUNDS_ reference_bounds\n' + reference + \
        '\n#undef BOUNDS_\n#define BOUNDS_ candidate_bounds\n' + functions + \
        '\ntypedef void (*operation)(psxCP2Regs *);\n' + \
        'static operation refs[] = {' + ','.join('reference_' + n for n in NAMES) + '};\n' + \
        'static operation candidates[] = {' + ','.join('gte' + n for n in NAMES) + '};\n' + \
        'static const char *names[] = {' + ','.join('"' + n + '"' for n in NAMES) + '};\n' + r'''
static u32 random_state = 0x9f133be7;
static u32 random32(void) {
    random_state ^= random_state << 13; random_state ^= random_state >> 17;
    random_state ^= random_state << 5; return random_state;
}
static psxCP2Regs inputs[128];
static u32 codes[128];
static void init_inputs(void) {
    const u32 edge[] = {0, 1, 0x7fff, 0x8000, 0xffff, 0x7fffffff, 0x80000000, 0xffffffff};
    for (unsigned i = 0; i < 128; i++) {
        for (unsigned r = 0; r < 32; r++) {
            inputs[i].CP2D.r[r] = i < 8 ? edge[i] : random32();
            inputs[i].CP2C.r[r] = i < 8 ? edge[i] : random32();
        }
        codes[i] = random32();
    }
}
static void check(void) {
    const s64 edges[] = {INT64_MIN, INT64_MAX, INT32_MIN, INT32_MAX,
        (s64)INT32_MIN - 1, (s64)INT32_MAX + 1, -1, 0, 1};
    for (unsigned i = 0; i < 10009; i++) {
        s64 value = i < 9 ? edges[i] : (s64)(((u64)random32() << 32) | random32());
        psxCP2Regs a = {0}, b = {0};
        a.CP2C.r[31] = b.CP2C.r[31] = random32();
        s64 ar = reference_bounds(&a, value, INT32_MAX, 0x80010000u, INT32_MIN, 0x80008000u);
        s64 br = candidate_bounds(&b, value, INT32_MAX, 0x80010000u, INT32_MIN, 0x80008000u);
        assert(ar == br && !memcmp(&a, &b, sizeof(a)));
    }
    for (unsigned pass = 0; pass < 32; pass++) {
        init_inputs();
        for (unsigned op = 0; op < 22; op++) for (unsigned i = 0; i < 128; i++) {
            psxCP2Regs a = inputs[i], b = a;
            psxRegs.code = codes[i]; refs[op](&a); candidates[op](&b);
            if (memcmp(&a, &b, sizeof(a))) {
                fprintf(stderr, "%s mismatch pass=%u input=%u\n", names[op], pass, i);
                abort();
            }
        }
    }
    puts("GTE: 90112 full-register comparisons passed");
}
#if defined(__sh__) && !defined(CHECK_ONLY)
static volatile u32 checksum;
volatile u32 gte_bench_done;
volatile u32 gte_bench_results[2][22][2];
static void typical_inputs(void) {
    for (unsigned i = 0; i < 128; i++) {
        psxCP2Regs *r = &inputs[i];
        for (unsigned j = 0; j < 3; j++) {
            r->CP2D.p[j*2].sw.l = (int)(random32() % 4001) - 2000;
            r->CP2D.p[j*2].sw.h = (int)(random32() % 4001) - 2000;
            r->CP2D.p[j*2+1].sw.l = (int)(random32() % 4001) - 2000;
            r->CP2D.p[12+j].sw.l = (int)(random32() % 2048) - 1024;
            r->CP2D.p[12+j].sw.h = (int)(random32() % 2048) - 1024;
        }
        for (unsigned j = 0; j < 3; j++) for (unsigned k = 0; k < 5; k++) {
            r->CP2C.p[j*8+k].sw.l = (int)(random32() % 8193) - 4096;
            r->CP2C.p[j*8+k].sw.h = (int)(random32() % 8193) - 4096;
        }
        r->CP2C.r[5] = 0; r->CP2C.r[6] = 0; r->CP2C.r[7] = 8192;
        r->CP2C.r[24] = 160 << 16; r->CP2C.r[25] = 120 << 16;
        r->CP2C.r[26] = 256; r->CP2C.r[27] = 0; r->CP2C.r[28] = 0;
        r->CP2C.r[29] = 1365; r->CP2C.r[30] = 1024;
        for (unsigned j = 8; j < 12; j++) r->CP2D.r[j] = random32() % 4096;
        for (unsigned j = 16; j < 20; j++) r->CP2D.r[j] = 1024 + random32() % 15000;
        codes[i] = 1 << 19;
    }
}
static uint64_t run(operation fn) {
    uint64_t begin = timer_us_gettime64();
    for (unsigned i = 0; i < 4096; i++) {
        psxCP2Regs r = inputs[i & 127];
        psxRegs.code = codes[i & 127]; fn(&r);
        checksum += r.CP2D.r[24] + r.CP2C.r[31];
    }
    return timer_us_gettime64() - begin;
}
#endif
int main(void) {
#ifdef __sh__
    dbgio_dev_select("fb");
#endif
    check();
#if defined(__sh__) && !defined(CHECK_ONLY)
    for (unsigned mode = 0; mode < 2; mode++) {
      if (mode) typical_inputs();
      for (unsigned op = 0; op < 22; op++) {
        uint64_t a = 0, b = 0;
        run(refs[op]); run(candidates[op]);
        for (unsigned pass = 0; pass < 4; pass++) {
            if (pass & 1) { b += run(candidates[op]); a += run(refs[op]); }
            else { a += run(refs[op]); b += run(candidates[op]); }
        }
        gte_bench_results[mode][op][0] = a;
        gte_bench_results[mode][op][1] = b;
        printf("%u %s ref=%llu candidate=%llu us\n", mode, names[op],
               (unsigned long long)a, (unsigned long long)b);
      }
    }
    gte_bench_done = 1;
    for (;;) thd_sleep(1000);
#endif
    return 0;
}
'''


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emit', type=Path, required=True)
    parser.add_argument('--production', action='store_true')
    args = parser.parse_args()
    args.emit.write_text(source(args.production))
