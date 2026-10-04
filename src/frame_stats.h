/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef BLOOM_FRAME_STATS_H
#define BLOOM_FRAME_STATS_H
#include <stdint.h>

/* Low-overhead presentation intervals, independent of subsystem profiling.
 * Read equal, nonzero, even sequence values before and after the snapshot.
 * Skip/decision counters are cumulative and count GPU scheduling decisions. */
struct bloom_frame_sample {
    uint32_t sequence, duration_ms, presentations, vblanks;
    uint32_t skip_decisions, total_decisions;
};
extern volatile struct bloom_frame_sample bloom_frame_sample;
#endif
