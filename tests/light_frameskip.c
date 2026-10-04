#include <assert.h>
#include <stdint.h>
#include <string.h>
#define noinline
#ifndef BLOOM_LIGHT_FRAME_PERIOD
#define BLOOM_LIGHT_FRAME_PERIOD 4
#endif
static uint32_t gpu_light_frameskip_stats[2];
static unsigned fills;
struct psx_gpu {
    struct {
        int set, cnt, active, frame_ready;
        const int *force, *advice;
        int *dirty;
        uint32_t light_phase, pending_fill[3];
    } frameskip;
};
static void do_cmd_list(uint32_t *list, int n, int *a, int *b, int *c) {
    assert(n == 3 && list[0]); fills++;
}
/* Insert production decision function here. */
int main(void) {
    int force = 1, advice = 1, dirty = 0;
    struct psx_gpu g = {0};
    g.frameskip.force = &force; g.frameskip.advice = &advice;
    g.frameskip.dirty = &dirty; g.frameskip.set = -2;
    for (unsigned i = 1; i <= 1000; i++) {
        int previous = g.frameskip.active;
        g.frameskip.pending_fill[0] = 2;
        unsigned before = fills;
        decide_frameskip(&g);
        assert(g.frameskip.active == (i % BLOOM_LIGHT_FRAME_PERIOD == 0));
        assert(!(previous && g.frameskip.active));
        assert(dirty == 1);
        assert(fills == before + !g.frameskip.active);
        assert(g.frameskip.pending_fill[0] == (g.frameskip.active ? 2 : 0));
    }
    assert(gpu_light_frameskip_stats[0] == 1000 / BLOOM_LIGHT_FRAME_PERIOD);
    assert(gpu_light_frameskip_stats[1] == 1000);
    /* Preserve existing fixed/auto schedules and force semantics. */
    force = advice = 0;
    for (int mode = -1; mode <= 3; mode++) {
        g.frameskip.set = mode; g.frameskip.active = 0; g.frameskip.cnt = 0;
        for (int i = 0; i < 32; i++) {
            decide_frameskip(&g);
            assert(g.frameskip.active == (mode > 0 && i % (mode + 1) < mode));
        }
    }
    g.frameskip.set = -1; g.frameskip.active = 0; advice = 1;
    decide_frameskip(&g); assert(g.frameskip.active);
    decide_frameskip(&g); assert(!g.frameskip.active);
    force = 1;
    for (int i = 0; i < 10; i++) { decide_frameskip(&g); assert(g.frameskip.active); }
    assert(gpu_light_frameskip_stats[1] == 1000);
    return 0;
}
