/* Standalone Dreamcast/Flycast exercise of Bloom's production AICA driver.
 * Left: 440 Hz. Right: 660 Hz. Two three-second passes test reopening.
 */
#include <kos.h>
#include <math.h>
#include "out.h"
#include "spu_config.h"

SPUConfig spu_config;
void out_register_aica(struct out_driver *driver);

/* This standalone test always exercises audible output, without menu state. */
int bloom_want_silent_audio(void) { return 0; }

int main(void)
{
    dbgio_dev_select("fb");
    struct out_driver driver;
    int16_t pcm[735 * 2];
    out_register_aica(&driver);
    printf("Bloom audio smoke: left 440 Hz, right 660 Hz\n");
    for (int pass = 0; pass < 2; pass++) {
        if (driver.init() != 0) {
            printf("BLOOM AUDIO SMOKE FAILED: initialization\n");
            return 1;
        }
        uint64_t start = timer_ms_gettime64();
        for (int frame = 0; frame < 180; frame++) {
            for (int i = 0; i < 735; i++) {
                double t = (frame * 735 + i) / 44100.0;
                pcm[2 * i] = (int16_t)(4096 * sin(2 * M_PI * 440 * t));
                pcm[2 * i + 1] = (int16_t)(4096 * sin(2 * M_PI * 660 * t));
            }
            driver.feed(pcm, sizeof(pcm));
            uint64_t deadline = start + (frame + 1) * 1000 / 60;
            uint64_t now = timer_ms_gettime64();
            if (deadline > now)
                thd_sleep(deadline - now);
        }
        for (int i = 0; i < 20; i++) {
            driver.feed(NULL, 0);
            thd_sleep(10);
        }
        driver.finish();
        printf("BLOOM AUDIO SMOKE: pass %d completed\n", pass + 1);
        thd_sleep(500);
    }
    printf("BLOOM AUDIO SMOKE COMPLETED (listen to verify sound)\n");
    for (;;) thd_sleep(1000);
}
