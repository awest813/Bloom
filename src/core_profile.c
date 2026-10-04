// SPDX-License-Identifier: GPL-2.0-only
/* Optional main-thread wall-time attribution. Nested wrapped calls charge
 * their own category, not their caller. Thread preemption remains charged
 * to the interrupted category, so these are not per-thread CPU counters. */
#include <arch/timer.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "core_profile.h"

enum profile_part { OTHER, PS1, DRAW, SOUND, VIDEO, MDEC, DISC,
	INTERPRETER, GEOMETRY, IO, PARTS };
static uint64_t elapsed[PARTS], stamp, epoch;
static unsigned int calls[PARTS], frames, presentations;
static enum profile_part active;
static int enabled;
_Static_assert(PARTS == BLOOM_PROFILE_PARTS, "Profiler snapshot layout mismatch");
volatile struct bloom_profile_sample bloom_profile_sample;
volatile struct bloom_gte_profile_sample bloom_gte_profile_sample;
static uint64_t gte_elapsed[BLOOM_GTE_PROFILE_PARTS];
static unsigned int gte_calls[BLOOM_GTE_PROFILE_PARTS];
volatile struct bloom_dispatch_profile_sample bloom_dispatch_profile_sample;
static struct bloom_dispatch_entry dispatch_entries[BLOOM_DISPATCH_PROFILE_SLOTS];
static uint32_t dispatch_dropped_calls, dispatch_dropped_us;
volatile struct bloom_io_profile_sample bloom_io_profile_sample;
static struct bloom_io_entry io_entries[BLOOM_IO_PROFILE_SLOTS];
static uint32_t io_dropped_calls, io_dropped_us;

static void io_record(unsigned kind, uint32_t address, uint32_t value, uint32_t usec)
{
	uint32_t offset = address & 0xffff;
	unsigned mode = 0;
	if (kind == 5 && offset == 0x10a8) {
		mode = value == 0x01000401 ? 1 : value == 0x01000201 ? 2
			: value == 0x01000200 ? 3 : 4;
	}
	if (offset >= 0x1c00 && offset < 0x2000) offset = 0x1c00;
	uint32_t key = (mode << 24) | (kind << 16) | offset;
	unsigned slot = ((offset >> 1) ^ (offset >> 8) ^ (kind * 17) ^ (mode * 13)) & (BLOOM_IO_PROFILE_SLOTS - 1);
	for (unsigned probe = 0; probe < 8; probe++) {
		struct bloom_io_entry *entry = &io_entries[slot];
		if (!entry->calls || entry->key == key) {
			entry->key = key;
			entry->calls++;
			entry->usec += usec;
			return;
		}
		slot = (slot + 1) & (BLOOM_IO_PROFILE_SLOTS - 1);
	}
	io_dropped_calls++;
	io_dropped_us += usec;
}

static void dispatch_record(uint32_t pc, uint32_t cycles, uint32_t usec)
{
	unsigned int slot = ((pc >> 2) ^ (pc >> 10)) & (BLOOM_DISPATCH_PROFILE_SLOTS - 1);
	/* Bound observer overhead when many PCs collide. Report dropped work. */
	for (unsigned int probe = 0; probe < 8; probe++) {
		struct bloom_dispatch_entry *entry = &dispatch_entries[slot];
		if (!entry->calls || entry->pc == pc) {
			if (!entry->calls) {
				entry->pc = pc;
				entry->requested_min = entry->requested_max = cycles;
			}
			entry->calls++;
			entry->usec += usec;
			if (cycles < entry->requested_min) entry->requested_min = cycles;
			if (cycles > entry->requested_max) entry->requested_max = cycles;
			return;
		}
		slot = (slot + 1) & (BLOOM_DISPATCH_PROFILE_SLOTS - 1);
	}
	dispatch_dropped_calls++;
	dispatch_dropped_us += usec;
}

void bloom_profile_reset(void)
{
	memset(elapsed, 0, sizeof(elapsed));
	memset(calls, 0, sizeof(calls));
	memset(gte_elapsed, 0, sizeof(gte_elapsed));
	memset(gte_calls, 0, sizeof(gte_calls));
	memset(dispatch_entries, 0, sizeof(dispatch_entries));
	dispatch_dropped_calls = dispatch_dropped_us = 0;
	memset(io_entries, 0, sizeof(io_entries));
	io_dropped_calls = io_dropped_us = 0;
	stamp = epoch = timer_us_gettime64();
	active = OTHER;
	frames = 0;
	presentations = 0;
	enabled = 1;
}

void bloom_profile_stop(void)
{
	enabled = 0;
}

void bloom_profile_present(void)
{
	if (enabled)
		presentations++;
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
	/* Publish only completed intervals. Debuggers can retrieve this without
	 * scraping a console or adding an output path to the emulated game. */
	bloom_profile_sample.sequence++;
	bloom_profile_sample.duration_us = duration > UINT32_MAX ? UINT32_MAX : (uint32_t)duration;
	bloom_profile_sample.vblanks = frames;
	bloom_profile_sample.presentations = presentations;
	for (unsigned int i = 0; i < PARTS; i++) {
		bloom_profile_sample.usec[i] = elapsed[i] > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed[i];
		bloom_profile_sample.calls[i] = calls[i];
	}
	bloom_gte_profile_sample.sequence++;
	bloom_gte_profile_sample.duration_us = bloom_profile_sample.duration_us;
	for (unsigned int i = 0; i < BLOOM_GTE_PROFILE_PARTS; i++) {
		bloom_gte_profile_sample.usec[i] = gte_elapsed[i] > UINT32_MAX
			? UINT32_MAX : (uint32_t)gte_elapsed[i];
		bloom_gte_profile_sample.calls[i] = gte_calls[i];
	}
	bloom_gte_profile_sample.sequence++;
	bloom_dispatch_profile_sample.sequence++;
	bloom_dispatch_profile_sample.duration_us = bloom_profile_sample.duration_us;
	bloom_dispatch_profile_sample.dropped_calls = dispatch_dropped_calls;
	bloom_dispatch_profile_sample.dropped_us = dispatch_dropped_us;
	for (unsigned int i = 0; i < BLOOM_DISPATCH_PROFILE_SLOTS; i++)
		bloom_dispatch_profile_sample.entries[i] = dispatch_entries[i];
	bloom_dispatch_profile_sample.sequence++;
	bloom_profile_sample.sequence++;
	bloom_io_profile_sample.sequence++;
	bloom_io_profile_sample.duration_us = bloom_profile_sample.duration_us;
	bloom_io_profile_sample.dropped_calls = io_dropped_calls;
	bloom_io_profile_sample.dropped_us = io_dropped_us;
	for (unsigned i = 0; i < BLOOM_IO_PROFILE_SLOTS; i++)
		bloom_io_profile_sample.entries[i] = io_entries[i];
	bloom_io_profile_sample.sequence++;
	printf("PROFILE: vblank=%.2f/s ps1=%.1f draw=%.1f spu=%.1f "
	       "video=%.1f mdec=%.1f disc=%.1f other=%.1f %% "
	       "calls=%u/%u/%u interp=%.1f gte=%.1f io=%.1f subcalls=%u/%u present=%.2f/s\n",
	       (double)frames * 1000000 / duration,
	       (double)(elapsed[PS1] + elapsed[INTERPRETER] + elapsed[GEOMETRY] + elapsed[IO]) * 100 / duration,
	       (double)elapsed[DRAW] * 100 / duration,
	       (double)elapsed[SOUND] * 100 / duration,
	       (double)elapsed[VIDEO] * 100 / duration,
	       (double)elapsed[MDEC] * 100 / duration,
	       (double)elapsed[DISC] * 100 / duration,
	       (double)elapsed[OTHER] * 100 / duration,
	       calls[PS1], calls[DRAW], calls[SOUND],
	       (double)elapsed[INTERPRETER] * 100 / duration,
	       (double)elapsed[GEOMETRY] * 100 / duration,
	       (double)elapsed[IO] * 100 / duration,
	       calls[INTERPRETER], calls[GEOMETRY],
	       (double)presentations * 1000000 / duration);
	memset(elapsed, 0, sizeof(elapsed));
	memset(calls, 0, sizeof(calls));
	memset(gte_elapsed, 0, sizeof(gte_elapsed));
	memset(gte_calls, 0, sizeof(gte_calls));
	memset(dispatch_entries, 0, sizeof(dispatch_entries));
	dispatch_dropped_calls = dispatch_dropped_us = 0;
	memset(io_entries, 0, sizeof(io_entries));
	io_dropped_calls = io_dropped_us = 0;
	frames = 0;
	presentations = 0;
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
struct block;
struct psxCP2Regs;
struct xa_decode;
/* Match psxhw.h: even narrow writes take a full 32-bit value. */
#define IO_READ(type, name, kind) \
	extern type __real_##name(uint32_t address); \
	type __wrap_##name(uint32_t address) { \
		enum profile_part previous = profile_enter(IO); \
		uint64_t before = elapsed[IO]; \
		type result = __real_##name(address); \
		profile_leave(previous); \
		if (enabled) io_record(kind, address, 0, (uint32_t)(elapsed[IO] - before)); \
		return result; \
	}
#define IO_WRITE(name, kind) \
	extern void __real_##name(uint32_t address, uint32_t value); \
	void __wrap_##name(uint32_t address, uint32_t value) { \
		enum profile_part previous = profile_enter(IO); \
		uint64_t before = elapsed[IO]; \
		__real_##name(address, value); \
		profile_leave(previous); \
		if (enabled) io_record(kind, address, value, (uint32_t)(elapsed[IO] - before)); \
	}
IO_READ(uint8_t, psxHwRead8, 0)
IO_READ(uint16_t, psxHwRead16, 1)
IO_READ(uint32_t, psxHwRead32, 2)
IO_WRITE(psxHwWrite8, 3)
IO_WRITE(psxHwWrite16, 4)
IO_WRITE(psxHwWrite32, 5)
#undef IO_READ
#undef IO_WRITE
extern uint32_t __real_lightrec_execute(struct lightrec_state *, uint32_t, uint32_t);
uint32_t __wrap_lightrec_execute(struct lightrec_state *s, uint32_t pc, uint32_t cycles)
{
	enum profile_part previous = profile_enter(PS1);
	uint64_t before = elapsed[PS1];
	uint32_t result = __real_lightrec_execute(s, pc, cycles);
	profile_leave(previous);
	if (enabled)
		dispatch_record(pc, cycles, (uint32_t)(elapsed[PS1] - before));
	return result;
}
WRAP_RET(uint32_t, lightrec_run_interpreter, PS1,
	(struct lightrec_state *s, uint32_t pc, uint32_t cycles), (s, pc, cycles))
WRAP_RET(uint32_t, lightrec_emulate_block, INTERPRETER,
	(struct lightrec_state *s, struct block *b, uint32_t pc), (s, b, pc))
/* External/JIT references reach these wrappers. Recursive interpreter calls
 * within its own object remain charged to the enclosing fallback call. */
#define GTE_WRAP(name, slot) \
	extern void __real_##name(struct psxCP2Regs *r); \
	void __wrap_##name(struct psxCP2Regs *r) { \
		enum profile_part previous = profile_enter(GEOMETRY); \
		uint64_t before = elapsed[GEOMETRY]; \
		__real_##name(r); \
		profile_leave(previous); \
		if (enabled) { \
			gte_elapsed[slot] += elapsed[GEOMETRY] - before; \
			gte_calls[slot]++; \
		} \
	}
GTE_WRAP(gteRTPS, 0)
GTE_WRAP(gteNCLIP, 1)
GTE_WRAP(gteOP, 2)
GTE_WRAP(gteDPCS, 3)
GTE_WRAP(gteINTPL, 4)
GTE_WRAP(gteMVMVA, 5)
GTE_WRAP(gteNCDS, 6)
GTE_WRAP(gteCDP, 7)
GTE_WRAP(gteNCDT, 8)
GTE_WRAP(gteNCCS, 9)
GTE_WRAP(gteCC, 10)
GTE_WRAP(gteNCS, 11)
GTE_WRAP(gteNCT, 12)
GTE_WRAP(gteSQR, 13)
GTE_WRAP(gteDCPL, 14)
GTE_WRAP(gteDPCT, 15)
GTE_WRAP(gteAVSZ3, 16)
GTE_WRAP(gteAVSZ4, 17)
GTE_WRAP(gteRTPT, 18)
GTE_WRAP(gteGPF, 19)
GTE_WRAP(gteGPL, 20)
GTE_WRAP(gteNCCT, 21)
#undef GTE_WRAP
/* PVR may defer these commands: work drained later stays in the category
 * active at that point. This is command handling, not total raster time. */
WRAP_RET(int, do_cmd_list, DRAW,
	(uint32_t *list, int count, int *sum, int *last, int *cmd),
	(list, count, sum, last, cmd))
WRAP_VOID(renderer_sync, DRAW, (void), ())
WRAP_VOID(renderer_sync_ecmds, DRAW, (uint32_t *p), (p))
WRAP_VOID(renderer_flush_queues, DRAW, (void), ())
WRAP_VOID(renderer_update_caches, DRAW,
	(int x, int y, int w, int h, int changed), (x, y, w, h, changed))
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
