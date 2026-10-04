#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define LE32TOH(x) (x)
#include "deps/pcsx_rearmed/libpcsxcore/lightrec/timer-poll.h"
static uint32_t rng=0xa79248b3;
static uint32_t random32(void) { rng^=rng<<13;rng^=rng>>17;return rng^=rng<<5; }
static const uint32_t instructions[]={
 0x95220000,0,0x3043ffff,0x0064102b,0x10400009,0,
 0x94c20000,0,0x10400004,0,0x94c20000,0x0800010e,0x00431821,0x006a1821,
 0x95020000,0,0x30420200,0x10400006,0x00641023,
 0x0045102b,0x14400007,0x2402fffe,0x08000154,0,
 0x000210c2,0x0045102b,0x10400039,0x2402fffe,
 0x8ce20000,0,0x30420080,0x1040ffe0,0};
static uint32_t reference(uint32_t *gpr,uint32_t cycle,uint32_t start,
                          uint16_t *mode,uint32_t budget)
{
    uint32_t elapsed=0, loops=0, v0=0, v1=0;
    /* Literal complete iterations of the proven branch path. Each MMIO
     * transition truncates its pending 1/1024-cycle fraction. */
    while(elapsed+32 < (budget>>10)) {
        uint32_t head_cycle=cycle+elapsed;
        uint32_t count=((head_cycle-start)>>3)&65535;
        if (head_cycle-start>0x7ffff || count<gpr[4]) break;
        v0=count; v1=v0&65535;
        v0=v1<gpr[4]; assert(!v0);
        v0=*mode;v0&=0x200;assert(v0);
        v0=v1-gpr[4];v0=v0<gpr[5];
        if(!v0) break;
        v0=0; /* I_STAT & 0x80 at the end of the complete iteration. */
        *mode&=0xe7ff; elapsed+=32;loops++;
    }
    if(loops) {gpr[2]=v0;gpr[3]=v1;}
    return elapsed*1024;
}
int main(void)
{
    uint8_t ram[1024]={0},before_ram[1024];
    const uint32_t body=128,pc=body;
    memcpy(ram+body,instructions,sizeof(instructions));memcpy(before_ram,ram,sizeof(ram));
    unsigned folded=0;
    for(unsigned i=0;i<20000;i++) {
        uint32_t a[34],b[34];for(unsigned j=0;j<34;j++)a[j]=random32();
        a[7]=0x1f801070;a[8]=0x1f801124;a[9]=0x1f801120;
        uint32_t elapsed=random32()%0x80020,cycle=random32(),start=cycle-elapsed;
        a[4]=i%3?0:random32()%65538;
        a[5]=i%3?65536:random32()%65538;
        memcpy(b,a,sizeof(a));uint16_t am=0x1a00,bm=am;
        uint32_t budget=(random32()%65536)*1024;
        uint32_t consumed=bloom_timer_poll((uintptr_t)ram,sizeof(ram),pc,a,
            cycle,start,8,&am,0,budget,1792);
        if(consumed) {
            /* Run only the folded instructions, with room for their final
             * branch, then compare all observable state. */
            uint32_t expected=reference(b,cycle,start,&bm,consumed+1024);
            assert(consumed==expected && consumed<budget);
            assert(!memcmp(a,b,sizeof(a)) && am==bm);folded++;
        } else assert(!memcmp(a,b,sizeof(a)) && am==bm);
        assert(!memcmp(ram,before_ram,sizeof(ram)));
    }
    assert(folded>10000);
    uint32_t g[34]={0};g[5]=65536;g[7]=0x1f801070;g[8]=0x1f801124;g[9]=0x1f801120;
    const unsigned executed[]={0,1,2,3,4,5,14,15,16,17,18,19,20,21,28,29,30,31,32};
    for(unsigned i=0;i<sizeof(executed)/sizeof(executed[0]);i++) {
        uint32_t offset=body+executed[i]*4,word;memcpy(&word,ram+offset,4);
        uint32_t changed=word^1;memcpy(ram+offset,&changed,4);
        uint16_t mode=0x1a00;uint32_t original[34];memcpy(original,g,sizeof(g));
        assert(!bloom_timer_poll((uintptr_t)ram,sizeof(ram),pc,g,2000,0,8,&mode,0,65536,1792));
        assert(mode==0x1a00 && !memcmp(g,original,sizeof(g)));memcpy(ram+offset,&word,4);
    }
    const uint32_t badpc[]={0,111,pc+1,1020,0x20000000+pc,0xa0000000+pc};
    for(unsigned i=0;i<sizeof(badpc)/sizeof(badpc[0]);i++) {
        uint16_t mode=0x1a00;
        assert(!bloom_timer_poll((uintptr_t)ram,sizeof(ram),badpc[i],g,2000,0,8,&mode,0,65536,1792));
    }
    for(unsigned i=0;i<6;i++) {
        uint16_t mode=i==0?0:0x1a00;
        uint32_t original[34];memcpy(original,g,sizeof(g));
        if(i==5)g[7]=0x1f801074;
        assert(!bloom_timer_poll((uintptr_t)ram,sizeof(ram),pc,g,
            i==4?0x80000:2000,0,i==1?1:8,&mode,i==2?0x80:0,65536,i==3?2048:1792));
        assert(mode==(i==0?0:0x1a00));memcpy(g,original,sizeof(g));
    }
    puts("20000 bounded timer-poll comparisons and rejection checks passed");
}
