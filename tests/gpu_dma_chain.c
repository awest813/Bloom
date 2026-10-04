#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef _arch_dreamcast
#include <kos.h>
#endif
#define LE32TOH(x) (x)
#define preload __builtin_prefetch
#define unlikely(x) __builtin_expect(!!(x),0)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define log_io(...)
#define log_anomaly(...)
#define PSX_GPU_STATUS_DHEIGHT (1u << 19)
static uint32_t frame=3,hcnt=7;
static struct mock_gpu {
 unsigned cmd_len; uint32_t cmd_buffer[1024],status;
 struct { uint32_t *frame_count,*hcnt;
  struct {uint32_t frame,hcnt,cycles,addr;} last_list;
 } state;
 uint32_t hash;
} gpu;
static uint32_t ram[0x200000/4];
static uint32_t rng=1234567;
static uint32_t random32(void) { rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng; }
static int do_cmd_buffer(struct mock_gpu *g,uint32_t *p,int n,int *sum,int *last) {
 int left=p[0]%4;if(left>n)left=n;
 for(int i=0;i<n-left;i++) g->hash=(g->hash^p[i])*16777619u;
 *sum+=*last;*last=(n-left)*3;return left;
}
static void flush_cmd_buffer(struct mock_gpu *g) {
 int sum=0,last=0,left=do_cmd_buffer(g,g->cmd_buffer,g->cmd_len,&sum,&last);
 if(left)memmove(g->cmd_buffer,g->cmd_buffer+g->cmd_len-left,left*4);
 g->cmd_len=left;
}
long reference_chain(uint32_t *rambase, uint32_t start_addr,
  uint32_t *progress_addr, int32_t *cycles_last_cmd)
{
  uint32_t addr, *list, ld_addr;
  int len, left, count, ld_count = 32;
  int cpu_cycles_sum = 0;
  int cpu_cycles_last = 0;
#if BLOOM_DMA_PROFILE
  bloom_dma_counters[0]++;
  bloom_dma_counters[5] += progress_addr != NULL;
#endif

  preload(rambase + (start_addr & 0x1fffff) / 4);

  if (unlikely(gpu.cmd_len > 0))
    flush_cmd_buffer(&gpu);

  log_io(&gpu, "gpu_dma_chain\n");
  addr = ld_addr = start_addr & 0xffffff;
  for (count = 0; (addr & 0x800000) == 0; count++)
  {
    list = rambase + (addr & 0x1fffff) / 4;
    len = LE32TOH(list[0]) >> 24;
    addr = LE32TOH(list[0]) & 0xffffff;
#if BLOOM_DMA_PROFILE
    bloom_dma_counters[1]++;
    bloom_dma_counters[2] += len == 0;
    bloom_dma_counters[3] += len;
#endif
    preload(rambase + (addr & 0x1fffff) / 4);

    cpu_cycles_sum += 10;
    if (len > 0)
      cpu_cycles_sum += 5 + len;

    log_io(&gpu, ".chain %08lx #%d+%d %u+%u\n",
      (long)(list - rambase) * 4, len, gpu.cmd_len, cpu_cycles_sum, cpu_cycles_last);
    if (unlikely(gpu.cmd_len > 0)) {
#if BLOOM_DMA_PROFILE
      bloom_dma_counters[4]++;
#endif
      if (gpu.cmd_len + len > ARRAY_SIZE(gpu.cmd_buffer)) {
        log_anomaly(&gpu, "cmd_buffer overflow, likely garbage commands\n");
        gpu.cmd_len = 0;
      }
      memcpy(gpu.cmd_buffer + gpu.cmd_len, list + 1, len * 4);
      gpu.cmd_len += len;
      flush_cmd_buffer(&gpu);
      continue;
    }

    if (len) {
      left = do_cmd_buffer(&gpu, list + 1, len, &cpu_cycles_sum, &cpu_cycles_last);
      if (left) {
        memcpy(gpu.cmd_buffer, list + 1 + len - left, left * 4);
        gpu.cmd_len = left;
        log_anomaly(&gpu, "reference_chain: %d/%d words left\n", left, len);
      }
    }

    if (progress_addr) {
      // hack for bios boot logo race (must be not too fast or too slow)
      if (gpu.status & PSX_GPU_STATUS_DHEIGHT)
        cpu_cycles_sum += 5;
      if (cpu_cycles_sum > 512)
        break;
    }
    if (addr == ld_addr) {
      log_anomaly(&gpu, "reference_chain: loop @ %08x, cnt=%u\n", addr, count);
      break;
    }
    if (count == ld_count) {
      ld_addr = addr;
      ld_count *= 2;
    }
  }

  //printf(" -> %d %d\n", cpu_cycles_sum, cpu_cycles_last);
  gpu.state.last_list.frame = *gpu.state.frame_count;
  gpu.state.last_list.hcnt = *gpu.state.hcnt;
  gpu.state.last_list.cycles = cpu_cycles_sum + cpu_cycles_last;
  gpu.state.last_list.addr = start_addr;

  if (progress_addr)
    *progress_addr = addr;
  *cycles_last_cmd = cpu_cycles_last;
  return cpu_cycles_sum;
}
/* Production DMA is inserted here by test_regressions.py. */

static void check(void) {
 for(unsigned test=0;test<20000;test++) {
  unsigned n=1+random32()%256,base=4096; int circular=test%13==0;
  memset(ram+base/4,0,256*64+64);
  for(unsigned j=0;j<n;j++) {
   unsigned addr=base+j*64,len=circular?0:(random32()%5?0:1+random32()%12);
   unsigned next=j+1<n?addr+64:0xffffff;
   if(circular && j==n-1)next=base+(random32()%n)*64;
   if(next!=0xffffff && test%7==0)next|=0x200000;
   if(next!=0xffffff && test%11==0)next|=3;
   ram[addr/4]=(len<<24)|next;
   for(unsigned k=1;k<=len;k++)ram[addr/4+k]=random32();
  }
  memset(&gpu,0,sizeof(gpu));gpu.state.frame_count=&frame;gpu.state.hcnt=&hcnt;
  gpu.status=test%3==0?PSX_GPU_STATUS_DHEIGHT:0;
  if(!circular && test%4==0){gpu.cmd_len=2;gpu.cmd_buffer[0]=random32();gpu.cmd_buffer[1]=random32();}
  struct mock_gpu initial=gpu;
  uint32_t pa=0,pb=0;int32_t la=0,lb=0; int walking=test%2;
  uint32_t start=test%17==0?0xffffff:base;
  long a=reference_chain(ram,start,walking?&pa:0,&la);struct mock_gpu expected=gpu;
  gpu=initial;long b=GPUdmaChain(ram,start,walking?&pb:0,&lb);
  assert(a==b && pa==pb && la==lb && !memcmp(&gpu,&expected,sizeof(gpu)));
 }
}
#ifdef _arch_dreamcast
volatile uint32_t dma_done,dma_results[4][3];
#endif
int main(void) {
 check();puts("20000 DMA chain cases match");
#ifdef _arch_dreamcast
 for(unsigned mode=0;mode<4;mode++) {
  unsigned n=2048;memset(ram,0,sizeof(ram));
  for(unsigned j=0;j<n;j++) {
   unsigned addr=0x10000+j*16,len=mode==0?0:(j%(mode==1?16:4)==0?2:0);
   ram[addr/4]=(len<<24)|(j?addr-16:0xffffff);
   ram[addr/4+1]=4;ram[addr/4+2]=8;
  }
  memset(&gpu,0,sizeof(gpu));gpu.state.frame_count=&frame;gpu.state.hcnt=&hcnt;
  for(unsigned variant=0;variant<2;variant++) {
   uint64_t begin=timer_us_gettime64();uint32_t sum=0,p=0;int32_t last;
   for(unsigned repeat=0;repeat<400;repeat++) sum+=variant?GPUdmaChain(ram,0x10000+(n-1)*16,mode==3?&p:0,&last):reference_chain(ram,0x10000+(n-1)*16,mode==3?&p:0,&last);
   dma_results[mode][variant]=timer_us_gettime64()-begin;dma_results[mode][2]=sum;
  }
 }
 dma_done=1;puts("DMA benchmark complete");while(1)thd_sleep(100);
#endif
}
