// SPDX-License-Identifier: GPL-2.0-only
/* Optional main-thread wall-time attribution. Nested wrapped calls charge
 * their own category, not their caller. Thread preemption remains charged
 * to the interrupted category, so these are not per-thread CPU counters. */
#include <arch/timer.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core_profile.h"

enum profile_part { OTHER, PS1, DRAW, SOUND, VIDEO, MDEC, DISC, PARTS };
static uint64_t elapsed[PARTS], stamp, epoch;
static unsigned int calls[PARTS], frames;
static enum profile_part active;
static int enabled;

void bloom_profile_reset(void)
{
	memset(elapsed, 0, sizeof(elapsed));
	memset(calls, 0, sizeof(calls));
	stamp = epoch = timer_us_gettime64();
	active = OTHER;
	frames = 0;
	enabled = 1;
}

void bloom_profile_stop(void)
{
	enabled = 0;
}

static enum profile_part profile_enter(enum profile_part part)
{
	enum profile_part previous = active;
	if (enabled) {
		uint64_t now = timer_us_gettime64();
		elapsed[active] += now - stamp;
		stamp = now;
		active = part;
		calls[part]++;
	}
	return previous;
}

static void profile_leave(enum profile_part previous)
{
	if (enabled) {
		uint64_t now = timer_us_gettime64();
		elapsed[active] += now - stamp;
		stamp = now;
		active = previous;
	}
}

static void profile_report(void)
{
	uint64_t now, duration;
	if (!enabled)
		return;
	frames++;
	now = timer_us_gettime64();
	if (now - epoch < 5000000)
		return;
	elapsed[active] += now - stamp;
	duration = now - epoch;
	printf("PROFILE: vblank=%.2f/s ps1=%.1f draw=%.1f spu=%.1f "
	       "video=%.1f mdec=%.1f disc=%.1f other=%.1f %% "
	       "calls=%u/%u/%u\n",
	       (double)frames * 1000000 / duration,
	       (double)elapsed[PS1] * 100 / duration,
	       (double)elapsed[DRAW] * 100 / duration,
	       (double)elapsed[SOUND] * 100 / duration,
	       (double)elapsed[VIDEO] * 100 / duration,
	       (double)elapsed[MDEC] * 100 / duration,
	       (double)elapsed[DISC] * 100 / duration,
	       (double)elapsed[OTHER] * 100 / duration,
	       calls[PS1], calls[DRAW], calls[SOUND]);
	memset(elapsed, 0, sizeof(elapsed));
	memset(calls, 0, sizeof(calls));
	frames = 0;
	/* Exclude serial printing from the next measurement interval. */
	stamp = epoch = timer_us_gettime64();
}

/* GNU ld wraps references from other objects, without patching dependencies.
 * Same-object calls remain inside their wrapped parent's category. */
#define WRAP_VOID(name, part, args, values) \
	extern void __real_##name args; \
	void __wrap_##name args { \
		enum profile_part previous = profile_enter(part); \
		__real_##name values; \
		profile_leave(previous); \
	}
#define WRAP_RET(type, name, part, args, values) \
	extern type __real_##name args; \
	type __wrap_##name args { \
		enum profile_part previous = profile_enter(part); \
		type result = __real_##name values; \
		profile_leave(previous); \
		return result; \
	}

struct lightrec_state;
struct xa_decode;
WRAP_RET(uint32_t, lightrec_execute, PS1,
	(struct lightrec_state *s, uint32_t pc, uint32_t cycles), (s, pc, cycles))
WRAP_RET(uint32_t, lightrec_run_interpreter, PS1,
	(struct lightrec_state *s, uint32_t pc, uint32_t cycles), (s, pc, cycles))
/* PVR may defer these commands: work drained later stays in the category
 * active at that point. This is command handling, not total raster time. */
WRAP_RET(int, do_cmd_list, DRAW,
	(uint32_t *list, int count, int *sum, int *last, int *cmd),
	(list, count, sum, last, cmd))
WRAP_VOID(SPUasync, SOUND, (unsigned int cycle, unsigned int flags), (cycle, flags))
WRAP_VOID(SPUwriteRegister, SOUND,
	(unsigned long reg, unsigned short value, unsigned int cycle), (reg, value, cycle))
WRAP_RET(unsigned short, SPUreadRegister, SOUND,
	(unsigned long reg, unsigned int cycle), (reg, cycle))
WRAP_VOID(SPUwriteDMAMem, SOUND,
	(unsigned short *p, int size, unsigned int cycle), (p, size, cycle))
WRAP_VOID(SPUreadDMAMem, SOUND,
	(unsigned short *p, int size, unsigned int cycle), (p, size, cycle))
WRAP_VOID(SPUplayADPCMchannel, SOUND,
	(struct xa_decode *p, unsigned int cycle, int start), (p, cycle, start))
WRAP_RET(int, SPUplayCDDAchannel, SOUND,
	(short *p, int size, unsigned int cycle, int start), (p, size, cycle, start))
WRAP_VOID(psxDma0, MDEC, (uint32_t a, uint32_t b, uint32_t c), (a, b, c))
WRAP_VOID(psxDma1, MDEC, (uint32_t a, uint32_t b, uint32_t c), (a, b, c))
WRAP_RET(int, cdra_readTrack, DISC, (const unsigned char *time), (time))

extern void __real_GPUupdateLace(void);
void __wrap_GPUupdateLace(void)
{
	enum profile_part previous = profile_enter(VIDEO);
	__real_GPUupdateLace();
	profile_leave(previous);
	profile_report();
}
