/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef LIGHTREC_COUNTDOWN_POLL_H
#define LIGHTREC_COUNTDOWN_POLL_H

#include <stdint.h>
#include <string.h>

static uint32_t countdown_word(uintptr_t ram, uint32_t offset)
{
	uint32_t value;
	memcpy(&value, (const void *)(ram + offset), sizeof(value));
	return LE32TOH(value);
}

/* Recognize a RAM polling loop with a stack-resident decrementing watchdog.
 * No game address is assumed. Both branches, all executed instructions and
 * delay slots must match. Only complete iterations before the next event are
 * folded; ordinary execution still handles timeout, loop exit and the event.
 * The caller excludes isolated RAM/custom memory handlers and invalidates the
 * returned store address just as an ordinary guest store would.
 */
static uint32_t lightrec_countdown_poll(uintptr_t ram, uint32_t size,
		uint32_t pc, uint32_t *gpr, uint32_t budget,
		uint32_t cycles_per_op, uint32_t *written)
{
	uint32_t first, body, stack, poll, counter, loops, cost, value;
	uint32_t load, high, low, stored;
	int tail;

	if (!cycles_per_op || cycles_per_op > UINT32_MAX / 56 ||
	    budget < 56 * cycles_per_op || size < 96 || (pc & 3) || pc > size - 96)
		return 0;
	first = countdown_word(ram, pc);
	tail = (first >> 16) == 0x3c02; /* lui v0, imm */
	if (tail) {
		if (pc < 72) return 0;
		body = pc - 72;
	} else {
		if ((first >> 16) != 0x8fa2) return 0; /* lw v0, off(sp) */
		body = pc;
	}
	load = countdown_word(ram, body);
	high = countdown_word(ram, body + 72);
	low = countdown_word(ram, body + 76);
	if ((load >> 16) != 0x8fa2 || (high >> 16) != 0x3c02 ||
	    (low >> 16) != 0x8c42 || gpr[3] != UINT32_MAX ||
	    countdown_word(ram, body + 4) != 0 ||
	    countdown_word(ram, body + 8) != 0x2442ffff ||
	    countdown_word(ram, body + 12) != (0xafa20000 | (load & 0xffff)) ||
	    countdown_word(ram, body + 16) != load ||
	    countdown_word(ram, body + 20) != 0 ||
	    countdown_word(ram, body + 24) != 0x1443000b ||
	    countdown_word(ram, body + 28) != 0 ||
	    countdown_word(ram, body + 80) != 0 ||
	    countdown_word(ram, body + 84) != 0x0044102a ||
	    countdown_word(ram, body + 88) != 0x1440ffe9 ||
	    countdown_word(ram, body + 92) != 0)
		return 0;

	stack = gpr[29] + (int16_t)load;
	poll = (high << 16) + (int16_t)low;
	/* Only canonical RAM and its cached alias, never MMIO. */
	if ((stack & 0x60000000) || (poll & 0x60000000)) return 0;
	stack &= 0x1fffffff;
	poll &= 0x1fffffff;
	if ((stack & 3) || (poll & 3) || stack > size - 4 || poll > size - 4 ||
	    stack == poll || (stack >= body && stack < body + 96))
		return 0;
	counter = countdown_word(ram, stack);
	value = countdown_word(ram, poll);
	if (!counter || (int32_t)value >= (int32_t)gpr[4]) return 0;
	cost = 14 * cycles_per_op;
	loops = (budget - 1) / cost;
	if (loops > counter) loops = counter;
	if (loops < 2) return 0;
	stored = HTOLE32(counter - loops);
	memcpy((void *)(ram + stack), &stored, sizeof(stored));
	gpr[2] = tail ? counter - loops : 1;
	*written = stack;
	return loops * cost;
}
#endif
