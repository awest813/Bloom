/* Native GNU Lightning division checks and timings. Build against each
 * library variant; division by zero is handled by Lightrec, not Lightning. */
#include <kos.h>
#include <lightning.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static uint8_t code_buffer[1024*1024] __attribute__((aligned(32)));
static jit_state_t *states[24];
typedef void (*division_fn)(uint32_t, uint32_t, uint32_t *);
static division_fn functions[24];
volatile uint32_t div_done, div_results[24], div_checks, div_failure;
static uint32_t rng=0xab821983;
static uint32_t random32(void) { rng^=rng<<13; rng^=rng>>17; return rng^=rng<<5; }

static void compile(unsigned kind, unsigned alias)
{
    unsigned slot=kind*4+alias;
    jit_state_t *_jit=states[slot]=jit_new_state();
    assert(_jit);
    jit_prolog();
    jit_node_t *a=jit_arg(), *b=jit_arg(), *out=jit_arg();
    jit_getarg(JIT_R1,a); jit_getarg(JIT_R2,b); jit_getarg(JIT_V0,out);
    int result=alias==1 ? JIT_R1 : alias==2 ? JIT_R2 : JIT_R0;
    int divisor=alias==3 ? JIT_R1 : JIT_R2;
    switch(kind) {
    case 0: jit_divr(result,JIT_R1,divisor); break;
    case 1: jit_divr_u(result,JIT_R1,divisor); break;
    case 2: jit_remr(result,JIT_R1,divisor); break;
    case 3: jit_remr_u(result,JIT_R1,divisor); break;
    case 4: jit_qdivr(result,JIT_V1,JIT_R1,divisor); break;
    case 5: jit_qdivr_u(result,JIT_V1,JIT_R1,divisor); break;
    }
    jit_stxi_i(0,JIT_V0,result);
    if(kind>=4) jit_stxi_i(4,JIT_V0,JIT_V1);
    jit_ret(); jit_epilog(); jit_realize();
    jit_word_t estimate=0, actual=0;
    jit_get_code(&estimate);
    assert(estimate>0 && estimate<=32768);
    jit_set_code(code_buffer+slot*32768,estimate);
    functions[slot]=(division_fn)jit_emit(); assert(functions[slot]);
    jit_get_code(&actual); assert(actual<=estimate);
    jit_clear_state();
}

static void check(unsigned kind, unsigned alias, uint32_t n, uint32_t d)
{
    unsigned slot=kind*4+alias;
    if(alias==3) d=n;
    if(!d) return;
    uint32_t q,r;
    if(!(kind&1)) {
        /* Widen to avoid C's INT_MIN/-1 overflow, then keep MIPS low bits. */
        int64_t sn=(int32_t)n, sd=(int32_t)d;
        q=(uint32_t)(sn/sd); r=(uint32_t)(sn%sd);
    } else { q=n/d; r=n%d; }
    uint32_t out[2]={0xdeadbeef,0xbadf00d};
    functions[slot](n,d,out);
    uint32_t expected=kind==2 || kind==3 ? r : q;
    if(out[0]!=expected || (kind>=4 && out[1]!=r)) {
        printf("DIV FAILURE kind=%u alias=%u n=%08lx d=%08lx got=%08lx/%08lx expected=%08lx/%08lx\n",
            kind,alias,(unsigned long)n,(unsigned long)d,(unsigned long)out[0],
            (unsigned long)out[1],(unsigned long)expected,(unsigned long)r);
        div_failure=slot+1;
        for(;;) thd_sleep(100);
    }
    div_checks++;
}

int main(int argc, char **argv)
{
    (void)argc; init_jit(argv[0]);
    const uint32_t edge[]={0,1,2,3,7,8,15,16,31,32,0xffff,0x10000,
        0x7fffffff,0x80000000,0x80000001,0xfffffffe,0xffffffff};
    for(unsigned k=0;k<6;k++) for(unsigned alias=0;alias<4;alias++) {
        compile(k,alias);
        for(unsigned i=0;i<sizeof(edge)/sizeof(edge[0]);i++)
            for(unsigned j=0;j<sizeof(edge)/sizeof(edge[0]);j++)
                check(k,alias,edge[i],edge[j]);
        for(unsigned i=0;i<10000;i++) check(k,alias,random32(),random32()|1);
        uint32_t out[2];
        uint64_t start=timer_us_gettime64();
        for(unsigned i=0;i<100000;i++) functions[k*4+alias](0xa3927651+i,137,out);
        div_results[k*4+alias]=timer_us_gettime64()-start;
    }
    printf("%lu generated-code division comparisons passed\n",(unsigned long)div_checks);
    div_done=1;
    for(;;) thd_sleep(100);
}
