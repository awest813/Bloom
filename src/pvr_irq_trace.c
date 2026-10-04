/* Diagnostic-only ordering capture. Never changes PVR state or IRQ handling. */
#include <stdint.h>
#include <dc/pvr.h>
#include <dc/sq.h>
#include <dc/asic.h>
#include <arch/irq.h>

extern volatile uint32_t pvr_state[];
volatile uint32_t bloom_pvr_events[256][16];
volatile uint32_t bloom_pvr_event_count, bloom_pvr_event_frozen;
volatile uint32_t bloom_pvr_last_packet[9];
volatile uint32_t bloom_pvr_fault_registers[8];

static void event(uint32_t kind, uint32_t argument) {
    int flags = irq_disable();
    if (!bloom_pvr_event_frozen) {
        unsigned n = bloom_pvr_event_count;
        volatile uint32_t *e = bloom_pvr_events[n & 255];
        e[0] = n; e[1] = kind; e[2] = argument;
        e[3] = pvr_state[12]; e[4] = pvr_state[14];
        e[5] = pvr_state[19]; e[6] = pvr_state[20];
        for (unsigned i=0; i<9; ++i) e[7+i] = bloom_pvr_last_packet[i];
        bloom_pvr_event_count = n + 1;
    }
    irq_restore(flags);
}
void __real_pvr_int_handler(uint32_t code, void *data);
void __wrap_pvr_int_handler(uint32_t code, void *data) {
    event(1, code);
    if (code == ASIC_EVT_PVR_OPAQUEMODDONE) {
        bloom_pvr_event_frozen = 1;
        const uintptr_t regs[] = {0xf7003e00, 0xf7003f00, 0xf6003e00,
            0xf6003f00, 0xff000010, 0xff000038, 0xff00003c};
        for (unsigned i=0; i<7; ++i)
            bloom_pvr_fault_registers[i] = *(volatile uint32_t *)regs[i];
        bloom_pvr_fault_registers[7] = arch_irq_get_context()->pc;
    }
    __real_pvr_int_handler(code, data);
}
void __real_pvr_blank_polyhdr(pvr_list_t list);
void __wrap_pvr_blank_polyhdr(pvr_list_t list) {
    event(2, list);
    __real_pvr_blank_polyhdr(list);
}
void *__real_pvr_sq_set32(void *dest, uint32_t c, size_t n, pvr_dma_type_t type);
void *__wrap_pvr_sq_set32(void *dest, uint32_t c, size_t n, pvr_dma_type_t type) {
    event(3, type);
    return __real_pvr_sq_set32(dest, c, n, type);
}
uint32_t *__real_sq_lock(void *dest);
uint32_t *__wrap_sq_lock(void *dest) {
    event(4, (uintptr_t)dest);
    return __real_sq_lock(dest);
}
