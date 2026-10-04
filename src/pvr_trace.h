/* Diagnostic-only packet staging and first format-error capture.
 * No reads are made from the write-only direct-render destination. */
#ifndef BLOOM_PVR_TRACE_H
#define BLOOM_PVR_TRACE_H
static void *bloom_ta_real_target(void) { return pvr_dr_target(); }
static uint32_t bloom_ta_stage[2][8] __attribute__((aligned(32)));
static unsigned bloom_ta_bank, bloom_ta_list = 7, bloom_ta_second, bloom_ta_wide;
static uint32_t bloom_ta_last_header;
volatile uint32_t bloom_ta_error_count;
volatile uint32_t bloom_ta_errors[16][12];

static void bloom_ta_error(unsigned line, unsigned kind, const uint32_t *packet)
{
    unsigned n = bloom_ta_error_count;
    if (n >= 16) return;
    bloom_ta_errors[n][0] = line;
    bloom_ta_errors[n][1] = kind;
    bloom_ta_errors[n][2] = bloom_ta_list;
    bloom_ta_errors[n][3] = bloom_ta_last_header;
    for (unsigned i = 0; i < 8; i++) bloom_ta_errors[n][4+i] = packet ? packet[i] : 0;
    bloom_ta_error_count = n + 1;
}
static int bloom_ta_list_begin(pvr_list_t list, unsigned line)
{
    if (bloom_ta_second) bloom_ta_error(line, 1, NULL);
    bloom_ta_list = list; bloom_ta_second = 0; bloom_ta_wide = 0;
    return pvr_list_begin(list);
}
static int bloom_ta_list_finish(unsigned line)
{
    if (bloom_ta_second) bloom_ta_error(line, 2, NULL);
    bloom_ta_second = 0; bloom_ta_list = 7;
    return pvr_list_finish();
}
static void bloom_ta_commit(const void *data, unsigned line)
{
    const uint32_t *p = data;
#ifdef __sh__
    extern volatile uint32_t bloom_pvr_last_packet[9];
    bloom_pvr_last_packet[0] = line;
    for (unsigned i = 0; i < 8; ++i) bloom_pvr_last_packet[i+1] = p[i];
#endif
    unsigned type = p[0] >> 29;
    if (bloom_ta_second) bloom_ta_second = 0;
    else if (type == 4) {
        if (((p[0] >> 24) & 7) != bloom_ta_list) bloom_ta_error(line, 3, p);
        bloom_ta_last_header = p[0];
        // Bloom uses packed-color polygon headers and 64-byte modifier vertices.
        bloom_ta_wide = (bloom_ta_list == PVR_LIST_TR_MOD)
            || ((p[0] & 0x48) == 0x48);
        if (bloom_ta_list != PVR_LIST_TR_MOD && (p[0] & 0x30)) bloom_ta_error(line, 4, p);
    } else if (type == 7) {
        bloom_ta_second = bloom_ta_wide;
    } else if (type != 1) {
        bloom_ta_error(line, 5, p);
    }
#ifdef __sh__
    if ((*(volatile uint32_t *)0xf7003e00 & 0x1ff00000) != 0x10000000)
        bloom_ta_error(line, 6, p);
#endif
    void *target = bloom_ta_real_target();
    copy32(target, data);
    pvr_dr_commit(target);
}
static uint32_t *bloom_ta_sq_lock(void *address, unsigned line)
{
    uint32_t *result = sq_lock(address);
#ifdef __sh__
    if ((*(volatile uint32_t *)0xf7003e00 & 0x1ff00000)
        != ((uintptr_t)address & 0x1ff00000))
        bloom_ta_error(line, 7, NULL);
#endif
    return result;
}
static void bloom_ta_sq_unlock(unsigned line)
{
    sq_unlock();
#ifdef __sh__
    if (bloom_ta_list != 7 &&
        (*(volatile uint32_t *)0xf7003e00 & 0x1ff00000) != 0x10000000)
        bloom_ta_error(line, 8, NULL);
#endif
}
#undef pvr_dr_target
#undef pvr_dr_commit
#define pvr_dr_target(...) ((void *)bloom_ta_stage[bloom_ta_bank ^= 1])
#define pvr_dr_commit(data) bloom_ta_commit(data, __LINE__)
#define pvr_list_begin(list) bloom_ta_list_begin(list, __LINE__)
#define pvr_list_finish() bloom_ta_list_finish(__LINE__)
#define sq_lock(address) bloom_ta_sq_lock(address, __LINE__)
#define sq_unlock() bloom_ta_sq_unlock(__LINE__)
#endif
