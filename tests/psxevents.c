#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32;
typedef int32_t s32;
#define PSXCLK 33868800
#define evprintf(...)
typedef struct { struct { u32 Cause, SR; } n; } psxCP0Regs;
typedef struct { u32 cycle, interrupt, next_interupt, event_cycles[15]; } psxRegisters;
static psxRegisters regs;
static u32 hw[2], log_ids[32], log_count, behavior;
static psxRegisters *cp0TOpsxRegs(psxCP0Regs *cp0) { (void)cp0; return &regs; }
#define psxHu32(x) hw[((x)-0x1070)/4]
static void mutate(unsigned id)
{
    log_ids[log_count++] = id;
    if (behavior & 1) regs.interrupt ^= (1u << ((id+3)%15));
    if (behavior & 2) regs.event_cycles[(id+5)%15] = regs.cycle + id * 31;
    if (behavior & 4) regs.cycle += id * 137;
    if (behavior & 8) hw[0] ^= id;
}
static void psxException(int a, int b, psxCP0Regs *cp0)
{ (void)a; (void)b; mutate(15); cp0->n.SR ^= 0x1234; }
#define IRQ(n) static void irq##n(void) { mutate(n); }
IRQ(0) IRQ(1) IRQ(2) IRQ(3) IRQ(4) IRQ(5) IRQ(6) IRQ(7)
IRQ(8) IRQ(9) IRQ(10) IRQ(11) IRQ(12) IRQ(13) IRQ(14)
static void (*const irq_funcs[])(void) = {irq0,irq1,irq2,irq3,irq4,irq5,irq6,irq7,irq8,irq9,irq10,irq11,irq12,irq13,irq14};

/* Production functions inserted here. */

/* Literal two-pass reference, retaining the original snapshot semantics. */
static void reference(psxCP0Regs *cp0)
{
    u32 cycle = regs.cycle, irq, bits;
    for (irq=0, bits=regs.interrupt; bits; irq++, bits >>= 1) {
        if (!(bits & 1)) continue;
        if ((s32)(cycle-regs.event_cycles[irq]) >= 0) {
            regs.interrupt &= ~(1u << irq);
            irq_funcs[irq]();
        }
    }
    cp0->n.Cause &= ~0x400;
    if (hw[0] & hw[1]) cp0->n.Cause |= 0x400;
    if (((cp0->n.Cause | 1) & cp0->n.SR & 0x401) == 0x401)
        psxException(0,0,cp0);
    cycle=regs.cycle;
    s32 minimum=PSXCLK;
    for (irq=0,bits=regs.interrupt; bits; irq++,bits>>=1) {
        if (!(bits&1)) continue;
        s32 diff=regs.event_cycles[irq]-cycle;
        if (0<diff && diff<minimum) minimum=diff;
    }
    regs.next_interupt=cycle+minimum;
}
static u32 rng=0x82618621;
static u32 rand32(void) { rng ^= rng<<13; rng ^= rng>>17; return rng ^= rng<<5; }
int main(void)
{
    for (unsigned trial=0; trial<100000; trial++) {
        regs.cycle=rand32(); regs.interrupt=rand32()&0x7fff;
        regs.next_interupt=rand32(); behavior=trial%16;
        for (unsigned i=0;i<15;i++) {
            u32 delta=rand32();
            if (trial%4==0) delta=delta%PSXCLK+1;
            if (trial%4==1) delta=(i%3==0 ? 0 : i%3==1 ? 0x80000000 : 0xffffffff);
            regs.event_cycles[i]=regs.cycle+delta;
        }
        hw[0]=rand32(); hw[1]=rand32();
        psxCP0Regs cp={{rand32(),rand32()}}, cp_ref=cp;
        psxRegisters initial=regs;
        u32 initial_hw[2]; memcpy(initial_hw,hw,sizeof(hw));
        log_count=0; memset(log_ids,0,sizeof(log_ids));
        reference(&cp_ref);
        psxRegisters expected=regs;
        u32 expected_hw[2], expected_log[32], count=log_count;
        memcpy(expected_hw,hw,sizeof(hw)); memcpy(expected_log,log_ids,sizeof(log_ids));
        regs=initial; memcpy(hw,initial_hw,sizeof(hw));
        log_count=0; memset(log_ids,0,sizeof(log_ids));
        gen_interupt(&cp);
        assert(!memcmp(&regs,&expected,sizeof(regs)));
        assert(!memcmp(&cp,&cp_ref,sizeof(cp)));
        assert(!memcmp(hw,expected_hw,sizeof(hw)));
        assert(count==log_count && !memcmp(log_ids,expected_log,sizeof(log_ids)));
    }
    puts("100000 event dispatch comparisons passed");
    return 0;
}
