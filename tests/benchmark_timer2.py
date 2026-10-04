"""Emit a timer-2 read differential test and optional SH-4 benchmark."""
import argparse
from pathlib import Path
from test_regressions import ROOT, extract_function

def source():
    original = (ROOT / 'deps/pcsx_rearmed/libpcsxcore/psxcounters.c').read_text()
    generic = extract_function(original, '_psxRcntRcount')
    candidate = extract_function(original, 'psxRcntRcount2').replace(
        'u32 psxRcntRcount2', 'static __attribute__((noinline)) u32 psxRcntRcount2')
    return r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
typedef uint32_t u32;
static struct { u32 cycle; } psxRegs;
static struct { u32 cycleStart, rate, mode; } rcnts[4];
#define verboseLog(...)
''' + generic + '\n' + candidate + r'''
static __attribute__((noinline)) u32 original_read(void) { return _psxRcntRcount(2); }
static u32 rng=0x5482abed;
static u32 rand32(void) { rng ^= rng<<13; rng ^= rng>>17; return rng ^= rng<<5; }
#ifdef __DREAMCAST__
#include <kos.h>
volatile u32 timer2_done, timer2_results[4][2], timer2_checksum;
static u32 time_reads(int candidate, u32 rate)
{
    rcnts[2].rate=rate; rcnts[2].cycleStart=0xfffffff0;
    uint64_t start=timer_us_gettime64();
    u32 sum=0;
    for (u32 i=0;i<100000;i++) {
        psxRegs.cycle=i*137;
        sum += candidate ? psxRcntRcount2() : original_read();
        __asm__ volatile("" ::: "memory");
    }
    u32 elapsed=timer_us_gettime64()-start;
    timer2_checksum=sum;
    return elapsed;
}
#endif
int main(void)
{
    for (unsigned i=0;i<100000;i++) {
        psxRegs.cycle=rand32(); rcnts[2].cycleStart=rand32();
        const u32 rates[]={0,1,8,0xffffffff,rand32()};
        for (unsigned j=0;j<5;j++) {
            rcnts[2].rate=rates[j];
            assert(psxRcntRcount2()==original_read());
        }
    }
    puts("500000 timer-2 comparisons passed");
#ifdef __DREAMCAST__
    const u32 rates[]={1,8,0xffffffff,5};
    for (unsigned i=0;i<4;i++) {
        timer2_results[i][0]=time_reads(0,rates[i]);
        timer2_results[i][1]=time_reads(1,rates[i]);
    }
    timer2_done=1;
    for (;;) thd_sleep(100);
#endif
    return 0;
}
'''

if __name__ == '__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('output',type=Path)
    args=parser.parse_args()
    args.output.write_text(source())
