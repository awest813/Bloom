#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#ifdef _arch_dreamcast
#include <kos.h>
#endif
#define BIT(n) (1u << (n))
static unsigned rng=1234567;
static unsigned random32(void) {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static uint64_t reference_mask(uint16_t umin, uint16_t umax,
                               uint16_t vmin, uint16_t vmax)
{
	uint64_t mask = 0, mask_horiz = 0;
	uint16_t u, v;

	/* 4x16 sub-blocks */

	for (u = umin & -64; u < umax; u += 64)
		mask_horiz |= BIT(u / 64);

	for (v = vmin & -16; v < vmax; v += 16)
		mask |= mask_horiz << (v / 4);

	return mask;
}
/* Production block mask is inserted here by test_regressions.py. */
static void check(void) {
 for(unsigned i=0;i<1000000;i++) {
  uint16_t a=random32()%257,b=random32()%257,c=random32()%257,d=random32()%257;
  assert(reference_mask(a,b,c,d)==get_block_mask(a,b,c,d));
 }
 for(unsigned x0=0;x0<=256;x0++)for(unsigned x1=0;x1<=256;x1++)
  for(unsigned y0=0;y0<=256;y0+=16) {
   unsigned y1=y0<256?y0+1:256;
   assert(reference_mask(x0,x1,y0,y1)==get_block_mask(x0,x1,y0,y1));
   assert(reference_mask(y0,y1,x0,x1)==get_block_mask(y0,y1,x0,x1));
  }
}
#ifdef _arch_dreamcast
volatile uint32_t mask_done,mask_results[2][2];
#endif
int main(void) {
 check();puts("Block masks match");
#ifdef _arch_dreamcast
 uint64_t (*volatile fn[2])(uint16_t,uint16_t,uint16_t,uint16_t)={reference_mask,get_block_mask};
 volatile uint64_t checksum=0;
 for(unsigned mode=0;mode<2;mode++)for(unsigned variant=0;variant<2;variant++) {
  uint64_t start=timer_us_gettime64();
  for(unsigned i=0;i<100000;i++)checksum^=fn[variant](i%64,192,mode?i%128:0,mode?128:256);
  mask_results[mode][variant]=timer_us_gettime64()-start;
 }
 mask_done=1;puts("Mask timing complete");while(1)thd_sleep(100);
#endif
}
