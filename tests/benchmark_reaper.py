#!/usr/bin/env python3
"""Compare empty cleanup dispatch with/without its atomic fast path.

Use --emit PATH to generate the same benchmark for the Dreamcast compiler.
This measures cleanup dispatch only, not game FPS.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from test_regressions import ROOT, extract_function


def source():
    reaper = (ROOT / "deps/lightrec/reaper.c").read_text()
    reaper = re.sub(r'^#include "[^"\n]+"\n', '', reaper, flags=re.M)
    reference = extract_function(reaper, "lightrec_reaper_reap")
    reference = reference.replace("void lightrec_reaper_reap(", "void reference_reap(")
    guard = ('\tif (!atomic_load_explicit(&reaper->pending, memory_order_acquire))\n'
             '\t\treturn;\n')
    assert guard in reference
    reference = reference.replace(guard, '')
    support = '#include "tests/reaper_support.h"\n'
    return support + reaper + reference + r'''
#ifdef __sh__
#include <kos.h>
#include <arch/timer.h>
#define ITERATIONS 100000
static uint64_t now_ns(void) { return timer_us_gettime64() * 1000; }
#else
#include <time.h>
#define ITERATIONS 5000000
static uint64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}
#endif
static uint64_t run(void (*volatile fn)(struct reaper *), struct reaper *r) {
    uint64_t start = now_ns();
    for (unsigned i = 0; i < ITERATIONS; i++) fn(r);
    return now_ns() - start;
}
int main(void) {
#ifdef __sh__
    dbgio_dev_select("fb");
#endif
    struct lightrec_state state = {0};
    struct reaper *r = lightrec_reaper_init(&state);
    if (!r) return 1;
    for (unsigned i = 0; i < 5; i++) {
        uint64_t old, fast;
        if (i & 1) {
            fast = run(lightrec_reaper_reap, r);
            old = run(reference_reap, r);
        } else {
            old = run(reference_reap, r);
            fast = run(lightrec_reaper_reap, r);
        }
        printf("REAPER: calls=%u locked=%.2f ns/call empty=%.2f ns/call ratio=%.2f\n",
            ITERATIONS, (double)old / ITERATIONS, (double)fast / ITERATIONS,
            fast ? (double)old / fast : 0);
    }
    lightrec_reaper_destroy(r);
#ifdef __sh__
    thd_sleep(60000);
#endif
    return 0;
}
'''


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emit", type=Path)
    args = parser.parse_args()
    if args.emit:
        args.emit.write_text(source(), encoding="utf-8")
    else:
        cc = os.environ.get("CC") or shutil.which("gcc") or shutil.which("clang")
        if not cc:
            parser.error("GCC or Clang is required")
        with tempfile.TemporaryDirectory() as tmp:
            src, exe = Path(tmp) / "bench.c", Path(tmp) / "bench"
            src.write_text(source(), encoding="utf-8")
            subprocess.run([cc, "-O2", "-std=gnu11", "-pthread", "-Wall", "-Wextra",
                            "-Werror", "-I" + str(ROOT),
                            str(src), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)
