#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define LE32TOH(x) (x)
#define HTOLE32(x) (x)
#include "deps/lightrec/countdown-poll.h"

static uint32_t rng = 0x316792ab;
static uint32_t random32(void) { rng ^= rng << 13; rng ^= rng >> 17; return rng ^= rng << 5; }
static uint32_t ram[4096], reference[4096], regs[34], refregs[34];
static void init(void)
{
	memset(ram, 0, sizeof(ram));
	for (unsigned i = 0; i < 34; i++) regs[i] = random32();
	regs[0] = 0; regs[3] = UINT32_MAX; regs[29] = 0x80002000;
	uint32_t *p = ram + 0x400 / 4;
	p[0]=0x8fa20010; p[2]=0x2442ffff; p[3]=0xafa20010;
	p[4]=p[0]; p[6]=0x1443000b;
	p[18]=0x3c028000; p[19]=0x8c423000;
	p[21]=0x0044102a; p[22]=0x1440ffe9;
	ram[0x2010/4] = random32() % 400 + 1;
	ram[0x3000/4] = random32();
	regs[4] = ram[0x3000/4] + 1;
}

/* Independent, instruction-by-instruction reference for this fixture's MIPS
 * subset. Load slots are NOPs; branch delay slots execute before the jump. */
static uint32_t reference_steps(uint32_t *memory, uint32_t *gpr, uint32_t pc,
		uint32_t count)
{
	uint32_t delayed = UINT32_MAX;
	for (uint32_t i = 0; i < count; i++) {
		uint32_t w=memory[pc/4], op=w>>26, rs=(w>>21)&31, rt=(w>>16)&31;
		uint32_t next=pc+4, pending=UINT32_MAX, addr;
		int32_t imm=(int16_t)w;
		if (!w) {}
		else if (op==0x23) { addr=(gpr[rs]+imm)&0x1fffffff; gpr[rt]=memory[addr/4]; }
		else if (op==0x2b) { addr=(gpr[rs]+imm)&0x1fffffff; memory[addr/4]=gpr[rt]; }
		else if (op==9) gpr[rt]=gpr[rs]+imm;
		else if (op==15) gpr[rt]=w<<16;
		else if (op==0 && (w&63)==42) gpr[(w>>11)&31]=(int32_t)gpr[rs]<(int32_t)gpr[rt];
		else if (op==5) pending=gpr[rs]!=gpr[rt] ? pc+4+imm*4 : pc+8;
		else assert(!"Unsupported reference instruction");
		if (delayed != UINT32_MAX) next=delayed;
		delayed=pending; pc=next;
	}
	assert(delayed==UINT32_MAX);
	return pc;
}

int main(void)
{
	unsigned accepted=0;
	for (unsigned trial=0; trial<12000; trial++) {
		init();
		uint32_t pc = trial & 1 ? 0x448 : 0x400;
		uint32_t cpo = random32()%4096+1;
		uint32_t budget = random32()%(cpo*14*256)+1;
		if (trial%11==0) ram[0x2010/4]=0;
		if (trial%13==0) ram[0x2010/4]=UINT32_MAX;
		if (trial%17==0) regs[4]=ram[0x3000/4];
		memcpy(reference,ram,sizeof(ram)); memcpy(refregs,regs,sizeof(regs));
		uint32_t written=UINT32_MAX;
		uint32_t used=lightrec_countdown_poll((uintptr_t)ram,sizeof(ram),pc,regs,budget,cpo,&written);
		if (used) {
			accepted++;
			assert(used<budget && used%cpo==0 && written==0x2010);
			assert(reference_steps(reference,refregs,pc,used/cpo)==pc);
		} else assert(written==UINT32_MAX);
		assert(!memcmp(reference,ram,sizeof(ram)));
		assert(!memcmp(refregs,regs,sizeof(regs)));
	}
	assert(accepted>8000);
	/* Every executed opcode, including delay slots, is necessary. */
	const unsigned offsets[]={0,1,2,3,4,5,6,7,18,19,20,21,22,23};
	for (unsigned i=0;i<sizeof(offsets)/sizeof(*offsets);i++) {
		init(); ram[0x400/4+offsets[i]] ^= 1u<<26;
		uint32_t written;
		assert(!lightrec_countdown_poll((uintptr_t)ram,sizeof(ram),0x400,regs,100000,1024,&written));
	}
	for (unsigned trial=0;trial<9;trial++) {
		init(); uint32_t pc=0x400, budget=100000, cpo=1024, written;
		switch(trial) {
		case 0: regs[29]=0x1f801000; break;
		case 1: regs[29]=0x80002ff0; break; /* store aliases polled word */
		case 2: regs[29]=0x800003f0; break; /* self-modifying loop */
		case 3: regs[29]++; break;
		case 4: regs[3]=0; break;
		case 5: cpo=0; break;
		case 6: cpo=UINT32_MAX; break;
		case 7: budget=0; break;
		case 8: pc=sizeof(ram)-4; break;
		}
		memcpy(reference,ram,sizeof(ram)); memcpy(refregs,regs,sizeof(regs));
		assert(!lightrec_countdown_poll((uintptr_t)ram,sizeof(ram),pc,regs,budget,cpo,&written));
		assert(!memcmp(reference,ram,sizeof(ram)) && !memcmp(refregs,regs,sizeof(regs)));
	}
	printf("Countdown poll differential checks passed (%u folded cases)\n",accepted);
	return 0;
}
