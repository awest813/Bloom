#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "deps/lightrec/lightrec.h"
/* Production opcode layout and functions are inserted here. */

static unsigned char memory[0x2000];
static unsigned reads;
static uint32_t callback_value, expected_opcode, expected_address;
static unsigned char callback_delay;
static void observe(struct lightrec_state *s, u32 opcode, void *host, u32 addr)
{
    assert(opcode == expected_opcode && addr == expected_address);
    assert(host == memory + addr - 0x1f801000);
    reads++;
    s->regs.gpr[17] ^= addr;
    s->in_delay_slot_n = callback_delay;
}
static u8 byte_read(struct lightrec_state *s,u32 op,void *p,u32 a)
{ observe(s,op,p,a); return callback_value; }
static u16 half_read(struct lightrec_state *s,u32 op,void *p,u32 a)
{ observe(s,op,p,a); return callback_value; }
static u32 word_read(struct lightrec_state *s,u32 op,void *p,u32 a)
{ observe(s,op,p,a); return callback_value; }
static const struct lightrec_mem_map_ops ops={.lb=byte_read,.lh=half_read,.lw=word_read};
static uint32_t rng=0x859acd17;
static uint32_t random32(void) { rng^=rng<<13; rng^=rng>>17; return rng^=rng<<5; }

int main(void)
{
    struct lightrec_mem_map maps[PSX_MAP_UNKNOWN]={0};
    struct lightrec_state s={0};
    s.maps=maps; s.nb_maps=PSX_MAP_UNKNOWN;
    maps[PSX_MAP_HW_REGISTERS]=(struct lightrec_mem_map){
        .pc=0x1f801000,.length=sizeof(memory),.address=memory,.ops=&ops};
    assert(lightrec_hw_read_map_ok(&s));
    for(unsigned i=0;i<PSX_MAP_HW_REGISTERS;i++) {
        maps[i]=(struct lightrec_mem_map){.pc=0x1f800fff,.length=2};
        assert(!lightrec_hw_read_map_ok(&s));
        maps[i].length=1; assert(lightrec_hw_read_map_ok(&s));
        maps[i].pc=0x1f803000; maps[i].length=4;
        assert(lightrec_hw_read_map_ok(&s));
        maps[i]=(struct lightrec_mem_map){0};
    }
    struct lightrec_mem_map *m=&maps[PSX_MAP_HW_REGISTERS];
    m->mirror_of=maps; assert(!lightrec_hw_read_map_ok(&s)); m->mirror_of=0;
    m->ops=0; assert(!lightrec_hw_read_map_ok(&s)); m->ops=&ops;
    m->length--; assert(!lightrec_hw_read_map_ok(&s)); m->length++;
    /* Production PCSX uses a larger aperture than the specialized window. */
    m->length=0x8000; assert(lightrec_hw_read_map_ok(&s));
    m->pc++; assert(!lightrec_hw_read_map_ok(&s)); m->pc--;
    m->address=0; assert(!lightrec_hw_read_map_ok(&s)); m->address=memory;
    s.nb_maps=PSX_MAP_HW_REGISTERS; assert(!lightrec_hw_read_map_ok(&s));
    s.nb_maps=PSX_MAP_UNKNOWN; s.direct_hw_read=true;
    const unsigned kinds[]={32,36,33,37,35};
    const uint32_t aliases[]={0,0x80000000,0xa0000000};
    for(unsigned i=0;i<50000;i++) {
        union code op={0}; unsigned kind=kinds[i%5];
        unsigned rs=random32()%32,rt=random32()%32;
        int16_t imm=(int16_t)random32();
        for(unsigned j=0;j<34;j++) s.regs.gpr[j]=random32();
        expected_address=0x1f801000+(random32()%sizeof(memory));
        op.opcode=expected_opcode=(kind<<26)|(rs<<21)|(rt<<16)|(uint16_t)imm;
        s.regs.gpr[rs]=(expected_address|aliases[i%3])-(int32_t)imm;
        s.temp_reg=random32(); s.in_delay_slot_n=random32();
        callback_value=random32(); callback_delay=i%4?0xff:0;
        struct lightrec_state expected=s;
        expected.regs.gpr[17]^=expected_address;
        expected.in_delay_slot_n=callback_delay;
        uint32_t result=callback_value;
        if(kind==32) result=(int8_t)result;
        if(kind==36) result=(uint8_t)result;
        if(kind==33) result=(int16_t)result;
        if(kind==37) result=(uint16_t)result;
        if(!callback_delay) { expected.temp_reg=result; expected.in_delay_slot_n=0xff; }
        else if(rt) expected.regs.gpr[rt]=result;
        reads=0;
        assert(lightrec_read_hw_fast(&s,op) && reads==1);
        assert(!memcmp(&s,&expected,sizeof(s)));
        /* Rejected operations must have no side effects. */
        for(unsigned rejected=0;rejected<4;rejected++) {
            struct lightrec_state before=s; union code bad=op;
            if(rejected==0) s.direct_hw_read=false;
            if(rejected==1) s.regs.gpr[rs]=0x1f800fff-(int32_t)imm;
            if(rejected==2) s.regs.gpr[rs]=0x1f803000-(int32_t)imm;
            if(rejected==3) bad.opcode=(43u<<26)|(rs<<21)|(rt<<16)|(uint16_t)imm;
            struct lightrec_state unchanged=s;
            reads=0; assert(!lightrec_read_hw_fast(&s,bad) && reads==0);
            assert(!memcmp(&s,&unchanged,sizeof(s))); s=before;
        }
    }
    puts("50000 direct hardware read comparisons passed");
}
