// SPDX-License-Identifier: GPL-2.0-only
/*
 * Portable pad mapping helpers
 *
 * Copyright (C) 2026 Bloom contributors
 */

#include "input_util.h"

/* Scale factor of analog sticks / 128.
 * sqrtf(128^2 + 128^2) == ~181.02f */
#define SCALE_FACTOR 181

uint8_t bloom_clamp8(int value)
{
	if (value < 0)
		return 0;
	if (value > 255)
		return 255;
	return (uint8_t)value;
}

uint8_t bloom_analog_scale(uint8_t val)
{
	return bloom_clamp8((int)val * SCALE_FACTOR / 128 + BLOOM_STICK_CENTER
			    - SCALE_FACTOR);
}

uint16_t bloom_button_combo(unsigned idx, int start_held, unsigned bit_yes,
			    unsigned bit_no, uint8_t *combo_mask)
{
	if (idx >= BLOOM_PAD_COUNT || !combo_mask)
		return 0;
	if (start_held) {
		*combo_mask |= (uint8_t)(1u << idx);
		return (uint16_t)(1u << bit_yes);
	}
	return (uint16_t)(1u << bit_no);
}

uint16_t bloom_start_buttons(int held, unsigned idx, uint8_t *start_mask,
			     uint8_t *old_start_mask, uint8_t *combo_mask,
			     unsigned start_bit)
{
	uint8_t bit;

	if (idx >= BLOOM_PAD_COUNT || !start_mask || !old_start_mask || !combo_mask)
		return 0;
	bit = (uint8_t)(1u << idx);

	if (held) {
		if (!(*start_mask & bit)) {
			*start_mask |= bit;
			*combo_mask &= (uint8_t)~bit;
		}
		return 0;
	}
	if (*old_start_mask & bit) {
		*old_start_mask &= (uint8_t)~bit;
		if (!(*combo_mask & bit))
			return (uint16_t)(1u << start_bit);
		return 0;
	}
	if (*start_mask & bit) {
		*start_mask &= (uint8_t)~bit;
		*old_start_mask |= bit;
		if (!(*combo_mask & bit))
			return (uint16_t)(1u << start_bit);
	}
	return 0;
}

int bloom_stick_deflected(uint8_t x, uint8_t y)
{
	return x < (BLOOM_STICK_CENTER - BLOOM_STICK_DEFLECT)
		|| y < (BLOOM_STICK_CENTER - BLOOM_STICK_DEFLECT)
		|| x > (BLOOM_STICK_CENTER + BLOOM_STICK_DEFLECT)
		|| y > (BLOOM_STICK_CENTER + BLOOM_STICK_DEFLECT);
}

void bloom_map_analog_combo(int start_held, uint8_t lx, uint8_t ly,
			    uint8_t rx, uint8_t ry, uint8_t *combo_mask,
			    unsigned idx, uint8_t *out_lx, uint8_t *out_ly,
			    uint8_t *out_rx, uint8_t *out_ry)
{
	if (!out_lx || !out_ly || !out_rx || !out_ry)
		return;

	if (start_held) {
		*out_rx = lx;
		*out_ry = ly;
		*out_lx = BLOOM_STICK_CENTER;
		*out_ly = BLOOM_STICK_CENTER;
		if (combo_mask && idx < BLOOM_PAD_COUNT
		    && bloom_stick_deflected(lx, ly))
			*combo_mask |= (uint8_t)(1u << idx);
		return;
	}

	*out_lx = lx;
	*out_ly = ly;
	*out_rx = rx;
	*out_ry = ry;
}

int bloom_pad_wants_multitap(unsigned idx, int use_multitap)
{
	return idx == 0 && use_multitap;
}

int bloom_rumble_should_run(int enabled, int low, int high)
{
	return enabled && (low || high);
}
