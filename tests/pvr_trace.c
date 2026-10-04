#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef unsigned pvr_list_t;
enum { PVR_LIST_TR_MOD = 3 };
static uint32_t destination[8] __attribute__((aligned(32)));
static unsigned submissions;
#define pvr_dr_target(...) ((void *)destination)
#define pvr_dr_commit(p) ((void)(p), submissions++)
static int pvr_list_begin(pvr_list_t list) { return 0; }
static int pvr_list_finish(void) { return 0; }
static uint32_t *sq_lock(void *address) { return address; }
static void sq_unlock(void) {}
static void copy32(void *d, const void *s) { memcpy(d, s, 32); }
#include "src/pvr_trace.h"
static void packet(uint32_t command) {
    uint32_t *p = pvr_dr_target();
    memset(p, 0, 32); p[0] = command; pvr_dr_commit(p);
    assert(destination[0] == command);
}
int main(void) {
    pvr_list_begin(2);
    packet(0x82000008); // single-volume texture: 32-byte vertices
    packet(0xe0000000); packet(0xf0000000);
    packet(0x820000c8); // two-volume texture: 64-byte vertices
    packet(0xf0000000); packet(0); // second part is data, not EOL
    pvr_list_finish();
    pvr_list_begin(3);
    packet(0x83000000); packet(0xf0000000); packet(0);
    pvr_list_finish();
    assert(bloom_ta_error_count == 0 && submissions == 9);
    pvr_list_begin(2); packet(0x81000000);
    assert(bloom_ta_error_count == 1 && bloom_ta_errors[0][1] == 3);
    packet(0x820000c8); packet(0xf0000000); pvr_list_finish();
    assert(bloom_ta_error_count == 2 && bloom_ta_errors[1][1] == 2);
    pvr_list_begin(2); packet(0);
    assert(bloom_ta_error_count == 3 && bloom_ta_errors[2][1] == 5);
    return 0;
}
