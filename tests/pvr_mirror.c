#include <assert.h>
#include <stdint.h>
#include <string.h>
#ifdef _arch_dreamcast
#include <kos.h>
#endif
#define likely(x) __builtin_expect(!!(x),1)
struct vertex_coords { int16_t x,y; uint16_t u,v; };
static uint32_t rng=7654321;
static unsigned random32(void) {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void reference_offset(const struct vertex_coords *coords,
			     float *uoff, float *voff)
{
	int x10 = coords[1].x - coords[0].x, y10 = coords[1].y - coords[0].y;
	int x20 = coords[2].x - coords[0].x, y20 = coords[2].y - coords[0].y;
	int u10 = coords[1].u - coords[0].u, u20 = coords[2].u - coords[0].u;
	int v10 = coords[1].v - coords[0].v, v20 = coords[2].v - coords[0].v;
	int det = x10 * y20 - x20 * y10;
	int n[4] = {
		u10 * y20 - u20 * y10,
		x10 * u20 - x20 * u10,
		v10 * y20 - v20 * y10,
		x10 * v20 - x20 * v10,
	};
	unsigned int i;

	if (det < 0) {
		det = -det;
		for (i = 0; i < 4; i++)
			n[i] = -n[i];
	}

	if (likely((n[0] | n[1] | n[2] | n[3]) >= 0 || det == 0))
		return;

	/* U reaches here shifted left by the texture depth (x1/x2/x4) */
	if (n[1] == 0) {
		if (n[0] == -det)
			*uoff = 15.0f / 16.0f;
		else if (n[0] == -2 * det)
			*uoff = 30.0f / 16.0f;
		else if (n[0] == -4 * det)
			*uoff = 60.0f / 16.0f;
	}
	if (n[3] == -det && n[2] == 0)
		*voff = 15.0f / 16.0f;
}
/* Production mirror offset is inserted here by test_regressions.py. */
static void compare(struct vertex_coords *v) {
 float a[2]={0.125f,0.25f},b[2]={a[0],a[1]};
 reference_offset(v,a,a+1);uv_mirror_offset(v,b,b+1);assert(!memcmp(a,b,sizeof(a)));
}
static void check(void) {
 for(unsigned i=0;i<1000000;i++) {
  struct vertex_coords v[3];
  for(unsigned j=0;j<3;j++)v[j]=(struct vertex_coords){(int)(random32()%4096)-2048,(int)(random32()%4096)-2048,random32()%1025,random32()%257};
  compare(v);
 }
 for(int x=1;x<=256;x*=2) for(int y=1;y<=256;y*=2)
 for(unsigned flip=0;flip<8;flip++) for(unsigned bpp=0;bpp<3;bpp++) {
  struct vertex_coords v[3]={{0,0,0,0},{x,0,x<<bpp,0},{0,y,0,y}};
  for(unsigned j=0;j<3;j++) {if(flip&1)v[j].u=(x<<bpp)-v[j].u;if(flip&2)v[j].v=y-v[j].v;}
  if(flip&4){struct vertex_coords tmp=v[1];v[1]=v[2];v[2]=tmp;}compare(v);
 }
 struct vertex_coords zero[3]={{0}};compare(zero);
}
#ifdef _arch_dreamcast
volatile uint32_t mirror_done,mirror_results[2][2];
#endif
int main(void) {
 check();
#ifdef _arch_dreamcast
 struct vertex_coords v[3]={{10,20,0,0},{100,30,128,20},{25,160,12,130}};
 void (*volatile fn[2])(const struct vertex_coords*,float*,float*)={reference_offset,uv_mirror_offset};
 for(unsigned mode=0;mode<2;mode++) {
  if(mode){v[1]=(struct vertex_coords){100,20,0,0};v[0].u=90;v[2]=(struct vertex_coords){10,150,90,130};}
  for(unsigned variant=0;variant<2;variant++) {
   float u=0,vv=0;uint64_t start=timer_us_gettime64();
   for(unsigned j=0;j<300000;j++)fn[variant](v,&u,&vv);
   mirror_results[mode][variant]=timer_us_gettime64()-start;
  }
 }
 mirror_done=1;puts("Mirror offsets match");while(1)thd_sleep(100);
#endif
}
