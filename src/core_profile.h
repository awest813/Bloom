// SPDX-License-Identifier: GPL-2.0-only
#ifndef BLOOM_CORE_PROFILE_H
#define BLOOM_CORE_PROFILE_H
#include <stdint.h>

/* Read-only diagnostic snapshot for external debuggers. Slot order is
 * other, ps1-exclusive, draw, sound, video, mdec, disc, interpreter, geometry, io.
 * Read sequence before and after the fields; accept only equal, nonzero,
 * even values. Never compare different binaries using a cached address. */
#define BLOOM_PROFILE_PARTS 10
struct bloom_profile_sample {
	uint32_t sequence;
	uint32_t duration_us;
	uint32_t vblanks;
	uint32_t presentations;
	uint32_t usec[BLOOM_PROFILE_PARTS];
	uint32_t calls[BLOOM_PROFILE_PARTS];
};
extern volatile struct bloom_profile_sample bloom_profile_sample;

/* Geometry slots follow the GTE_WRAP list in core_profile.c. */
#define BLOOM_GTE_PROFILE_PARTS 22
struct bloom_gte_profile_sample {
	uint32_t sequence, duration_us;
	uint32_t usec[BLOOM_GTE_PROFILE_PARTS];
	uint32_t calls[BLOOM_GTE_PROFILE_PARTS];
};
extern volatile struct bloom_gte_profile_sample bloom_gte_profile_sample;

/* Entry-PC attribution for lightrec_execute, not an instruction sampler.
 * Requested limits use Lightrec cycle units; no deadline is changed. */
#define BLOOM_DISPATCH_PROFILE_SLOTS 256
struct bloom_dispatch_entry {
	uint32_t pc, calls, usec, requested_min, requested_max;
};
struct bloom_dispatch_profile_sample {
	uint32_t sequence, duration_us, dropped_calls, dropped_us;
	struct bloom_dispatch_entry entries[BLOOM_DISPATCH_PROFILE_SLOTS];
};
extern volatile struct bloom_dispatch_profile_sample bloom_dispatch_profile_sample;

/* Register I/O attribution. Kind is 0/1/2 for reads of 8/16/32 bits and
 * 3/4/5 for writes. SPU registers are grouped at offset 0x1c00.
 * Key bits 24..31 distinguish GPU DMA: 1 chain, 2 upload, 3 read, 4 other. */
#define BLOOM_IO_PROFILE_SLOTS 128
struct bloom_io_entry { uint32_t key, calls, usec; };
struct bloom_io_profile_sample {
	uint32_t sequence, duration_us, dropped_calls, dropped_us;
	struct bloom_io_entry entries[BLOOM_IO_PROFILE_SLOTS];
};
extern volatile struct bloom_io_profile_sample bloom_io_profile_sample;

/* Call only around guest execution, outside any profiled entry point. */
void bloom_profile_reset(void);
void bloom_profile_stop(void);
void bloom_profile_present(void);

#endif
