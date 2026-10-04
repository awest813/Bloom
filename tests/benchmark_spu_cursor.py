#!/usr/bin/env python3
"""Compare production SPU no-interpolation cursors with the generic loop."""
import argparse
from pathlib import Path
from test_regressions import ROOT, extract_function


def source():
    text = (ROOT / 'deps/pcsx_rearmed/plugins/dfsound/spu.c').read_text()
    begin = text.index('#define make_do_samples(')
    macro = text[begin:text.index('// helpers for simple', begin)]
    candidate = extract_function(text, 'do_samples_nointerp')
    return r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __sh__
#include <kos.h>
#include <arch/timer.h>
#endif
#ifndef CHECK_ONLY
#ifdef __sh__
static uint64_t now_us(void) { return timer_us_gettime64(); }
#else
#include <time.h>
static uint64_t now_us(void) {
 struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
 return (uint64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000;
}
#endif
#endif
#define noinline __attribute__((noinline))
typedef struct { int SB[36]; } sample_buf;
''' + macro + r'''
make_do_samples(reference, , fa = sb->SB[29], , dst[ns] = fa, sb->SB[29] = fa)
''' + candidate + r'''
struct context { int *phase, *pos; unsigned calls, hash, mutate; };
static int decode(void *opaque, int ch, int *samples) {
 struct context *c = opaque;
 c->hash = c->hash * 33 + *c->phase + *c->pos + ch;
 ++c->calls;
 for (int i = 0; i < 28; ++i)
  samples[i] = (int16_t)(c->calls * 413u + i * 731u);
 if (c->mutate && c->calls % 3 == 0) {
  *c->pos = 2;
  *c->phase += 17;
 }
 return c->calls % 5 == 0;
}
static uint32_t rng = 1;
static uint32_t random_value(void) {
 rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng;
}
static void check(void) {
 static const int pitches[] = {0, 1, 0xffff, 0x10000, 0x10001, 0x3fff0};
 for (int trial = 0; trial < 3000; ++trial) {
  sample_buf a, b;
  int outa[920], outb[920];
  for (int i = 0; i < 36; ++i) a.SB[i] = (int16_t)random_value();
  b = a;
  memset(outa, 0x55, sizeof(outa)); memcpy(outb, outa, sizeof(outa));
  int pa = random_value() & 0xffff, pb = pa;
  int ia = trial % 28, ib = ia;
  int count = trial < 10 ? trial : (int)(random_value() % 920);
  int pitch = trial % 2 ? pitches[trial % 6] : (int)(random_value() % 0x40000);
  struct context ca = {&pa, &ia, 0, 0, trial % 2};
  struct context cb = {&pb, &ib, 0, 0, trial % 2};
  int ra = reference(outa, decode, &ca, 3, count, &a, pitch, &pa, &ia);
  int rb = do_samples_nointerp(outb, decode, &cb, 3, count, &b, pitch, &pb, &ib);
  if (ra != rb || pa != pb || ia != ib || ca.hash != cb.hash || ca.calls != cb.calls
      || memcmp(&a, &b, sizeof(a)) || memcmp(outa, outb, sizeof(outa))) {
   printf("FAIL trial %d count %d pitch %d\n", trial, count, pitch); exit(1);
  }
 }
 puts("SPU cursor comparisons passed (3000 cases)");
}
#ifndef CHECK_ONLY
typedef int (*sampler)(int *, int (*)(void *, int, int *), void *, int,
 int, sample_buf *, int, int *, int *);
static uint64_t run(sampler volatile fn, int pitch) {
 sample_buf sb = {{0}}; int out[735], phase = 0, pos = 27;
 struct context ctx = {&phase, &pos, 0, 0, 0};
 uint64_t start = now_us();
 for (int i = 0; i < 240; ++i)
  fn(out, decode, &ctx, 0, 735, &sb, pitch, &phase, &pos);
 return now_us() - start;
}
#endif
int main(void) {
#ifdef __sh__
 dbgio_dev_select("fb");
#endif
 check();
#ifndef CHECK_ONLY
 const int pitches[] = {0x4000, 0x10000, 0x28000};
 for (unsigned i = 0; i < 3; ++i) {
  uint64_t a = 0, b = 0;
  run(reference, pitches[i]); run(do_samples_nointerp, pitches[i]);
  for (unsigned pass = 0; pass < 4; ++pass) {
   if (pass & 1) { b += run(do_samples_nointerp, pitches[i]); a += run(reference, pitches[i]); }
   else { a += run(reference, pitches[i]); b += run(do_samples_nointerp, pitches[i]); }
  }
  printf("pitch=%x reference=%llu candidate=%llu us\n", pitches[i],
   (unsigned long long)a, (unsigned long long)b);
 }
#endif
#ifdef __sh__
 for (;;) thd_sleep(1000);
#endif
 return 0;
}
'''


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emit', type=Path, required=True)
    args = parser.parse_args()
    args.emit.write_text(source())
