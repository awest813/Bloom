#include <assert.h>
#include <stdint.h>
#include <string.h>
#define noinline
#define LE32TOH(x) (x)
#define HTOLE32(x) (x)
struct psx_gpu {
    uint32_t ex_regs[8];
    struct { uint32_t pending_fill[3]; } frameskip;
    struct { unsigned int w, h; } screen;
};
static unsigned char cmd_lengths[256];
static uint32_t synced_page;
static int decide_frameskip_allow(struct psx_gpu *gpu) { return 1; }
static void renderer_sync_ecmds(uint32_t *regs) { synced_page = regs[1]; }
static void do_cmd_list(uint32_t *list, int n, int *a, int *b, int *c) {
    assert(0 && "unexpected drawing during skipped polygon");
}
/* Insert production skip parser here. */
int main(void) {
    struct psx_gpu gpu = {0};
    gpu.screen.w = 320; gpu.screen.h = 240;
    for (unsigned cmd = 0x24; cmd <= 0x3f; cmd++) {
        if (!(cmd & 4)) continue;
        unsigned gouraud = !!(cmd & 16), quad = !!(cmd & 8);
        unsigned words = (gouraud ? 3 : 2) * (quad ? 4 : 3) + !gouraud;
        cmd_lengths[cmd] = words - 1;
        for (unsigned page = 0; page < 512; page++) {
            uint32_t packet[12] = {cmd << 24};
            packet[4 + gouraud] = (page << 16) | (page ^ 0x1ff);
            gpu.ex_regs[1] = 0xe1000600;
            int last = -1;
            assert(do_cmd_list_skip(&gpu, packet, words, &last) == (int)words);
            assert(last == (int)cmd);
            assert(gpu.ex_regs[1] == (0xe1000600 | page));
            assert(synced_page == gpu.ex_regs[1]);
            /* An incomplete polygon must not change inherited sprite state. */
            gpu.ex_regs[1] = 0xe1000600;
            assert(do_cmd_list_skip(&gpu, packet, words - 1, &last) == 0);
            assert(last == -1 && gpu.ex_regs[1] == 0xe1000600);
        }
    }
    return 0;
}
