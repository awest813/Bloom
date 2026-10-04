/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef BLOOM_TIMER_POLL_H
#define BLOOM_TIMER_POLL_H
#include <stdint.h>
#include <string.h>

static int bloom_poll_mmio(uint32_t value, uint32_t address)
{
    return value == address || value == address + 0x80000000u ||
           value == address + 0xa0000000u;
}

/* Recognize the count-read head, including every executed branch and slot.
 * Recheck at runtime as well as compilation to reject changed guest code. */
static int bloom_timer_poll_pattern(uintptr_t ram, uint32_t size, uint32_t pc)
{
    static const struct { uint8_t offset; uint32_t word; } pattern[] = {
        {0,0x95220000}, {4,0}, {8,0x3043ffff}, {12,0x0064102b},
        {16,0x10400009}, {20,0},
        {56,0x95020000}, {60,0}, {64,0x30420200}, {68,0x10400006},
        {72,0x00641023}, {76,0x0045102b}, {80,0x14400007}, {84,0x2402fffe},
        {112,0x8ce20000}, {116,0}, {120,0x30420080}, {124,0x1040ffe0}, {128,0}
    };
    if ((pc & 0x60000003) || size < 132) return 0;
    pc &= 0x1fffffff;
    if (pc > size - 132) return 0;
    /* Most entry PCs fail this first inexpensive load. */
    for (unsigned i=0;i<sizeof(pattern)/sizeof(pattern[0]);i++) {
        uint32_t word;
        memcpy(&word,(const void *)(ram+pc+pattern[i].offset),sizeof(word));
        if (LE32TOH(word) != pattern[i].word) return 0;
    }
    return 1;
}

/* Fold whole iterations at the compiled timer-count read. That read
 * truncates pending fractional cycles; mode is sampled at +10 and the next
 * count read at +32. Leave the event/timeout iteration to normal execution. */
static uint32_t bloom_timer_poll(uintptr_t ram, uint32_t size,
        uint32_t pc, uint32_t *gpr, uint32_t cycle, uint32_t cycle_start,
        uint32_t rate, uint16_t *mode, uint32_t irq_status,
        uint32_t budget, uint32_t cycles_per_op)
{
    uint32_t elapsed, first, limit, loops, by_counter;
    if (cycles_per_op != 1792 || budget < 65*1024 || rate != 8 ||
        !(*mode & 0x200) || (irq_status & 0x80) ||
        !bloom_poll_mmio(gpr[7],0x1f801070) ||
        !bloom_poll_mmio(gpr[8],0x1f801124) ||
        !bloom_poll_mmio(gpr[9],0x1f801120) ||
        !bloom_timer_poll_pattern(ram,size,pc)) return 0;
    elapsed = cycle - cycle_start;
    if (elapsed > 0x7ffff || gpr[4] > 0xffff || !gpr[5]) return 0;
    first = elapsed >> 3;
    if (first < gpr[4] || first - gpr[4] >= gpr[5]) return 0;
    limit = gpr[5] > 65536-gpr[4] ? 65535 : gpr[4]+gpr[5]-1;
    by_counter = ((limit-first) >> 2) + 1;
    loops = ((budget >> 10)-1) >> 5;
    if (loops > by_counter) loops = by_counter;
    if (loops < 2) return 0;
    gpr[2] = 0;
    gpr[3] = first + (loops-1)*4;
    *mode &= 0xe7ff;
    return loops << 15;
}
#endif
