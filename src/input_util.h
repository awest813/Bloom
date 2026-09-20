/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Portable pad mapping helpers (no KallistiOS)
 *
 * Copyright (C) 2026 Bloom contributors
 */

#ifndef BLOOM_INPUT_UTIL_H
#define BLOOM_INPUT_UTIL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLOOM_PAD_COUNT 8
#define BLOOM_STICK_CENTER 128
#define BLOOM_STICK_DEFLECT 64

uint8_t bloom_clamp8(int value);
uint8_t bloom_analog_scale(uint8_t val);

uint16_t bloom_button_combo(unsigned idx, int start_held, unsigned bit_yes,
			    unsigned bit_no, uint8_t *combo_mask);

uint16_t bloom_start_buttons(int held, unsigned idx, uint8_t *start_mask,
			     uint8_t *old_start_mask, uint8_t *combo_mask,
			     unsigned start_bit);

int bloom_stick_deflected(uint8_t x, uint8_t y);
void bloom_map_analog_combo(int start_held, uint8_t lx, uint8_t ly,
			    uint8_t rx, uint8_t ry, uint8_t *combo_mask,
			    unsigned idx, uint8_t *out_lx, uint8_t *out_ly,
			    uint8_t *out_rx, uint8_t *out_ry);

int bloom_pad_wants_multitap(unsigned idx, int use_multitap);
int bloom_rumble_should_run(int enabled, int low, int high);

#ifdef __cplusplus
}
#endif

#endif /* BLOOM_INPUT_UTIL_H */
