// SPDX-License-Identifier: GPL-2.0-only
/*
 * Input code
 *
 * Copyright (C) 2025 Paul Cercueil <paul@crapouillou.net>
 */

#include <frontend/plugin_lib.h>
#include <psemu_plugin_defs.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/maple/mouse.h>
#include <dc/maple/purupuru.h>
#include <kos/regfield.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#include "emu.h"
#include "input_util.h"
#include "settings.h"

unsigned short in_keystate[8];

static bool use_multitap;
static uint8_t start_mask;
static uint8_t old_start_mask;
static uint8_t combo_mask;

int in_type[8] = {
   PSE_PAD_TYPE_NONE, PSE_PAD_TYPE_NONE,
   PSE_PAD_TYPE_NONE, PSE_PAD_TYPE_NONE,
   PSE_PAD_TYPE_NONE, PSE_PAD_TYPE_NONE,
   PSE_PAD_TYPE_NONE, PSE_PAD_TYPE_NONE
};

static void emu_attach_cont_cb(maple_device_t *dev, void *)
{
	if (dev->port >= BLOOM_PAD_COUNT)
		return;

	if (cont_has_capabilities(dev, 0xffff3f00)) {
		printf("Plugged a BlueRetro / usb4maple controller in port %u\n",
		       dev->port);
	} else {
		printf("Plugged a standard controller in port %u\n", dev->port);
	}

	in_type[dev->port] = bloom_settings_analog()
		? PSE_PAD_TYPE_ANALOGPAD : PSE_PAD_TYPE_STANDARD;

	if (dev->port > 1) {
		/* Plugged in port C/D - enable multitap on player 1 */
		if (!use_multitap)
			printf("Enabling multi-tap\n");
		use_multitap = true;
	}
}

static void emu_detach_cb(maple_device_t *dev, void *)
{
	if (dev->port >= BLOOM_PAD_COUNT)
		return;

	printf("Unplugged input device from port %u\n", dev->port);
	in_type[dev->port] = PSE_PAD_TYPE_NONE;

	if (dev->port > 1) {
		/* Unplugged from port C/D - check if the other one is unplugged
		 * as well, and if it is, disable multitap */
		if (in_type[dev->port ^ 1] == PSE_PAD_TYPE_NONE) {
			if (use_multitap)
				printf("Disabling multi-tap\n");
			use_multitap = false;
		}
	}
}

static void emu_attach_mouse_cb(maple_device_t *dev, void *)
{
	if (dev->port >= BLOOM_PAD_COUNT)
		return;
	printf("Plugged a mouse in port %u\n", dev->port);
	in_type[dev->port] = PSE_PAD_TYPE_MOUSE;
}

void input_init(void) {
	maple_device_t *dev;
	unsigned int i;

	maple_attach_callback(MAPLE_FUNC_CONTROLLER, emu_attach_cont_cb, NULL);
	maple_attach_callback(MAPLE_FUNC_MOUSE, emu_attach_mouse_cb, NULL);

	maple_detach_callback(MAPLE_FUNC_CONTROLLER, emu_detach_cb, NULL);
	maple_detach_callback(MAPLE_FUNC_MOUSE, emu_detach_cb, NULL);

	for (i = 0; i < 4; i++) {
		dev = maple_enum_type(i, MAPLE_FUNC_CONTROLLER);
		if (dev)
			emu_attach_cont_cb(dev, NULL);

		dev = maple_enum_type(i, MAPLE_FUNC_MOUSE);
		if (dev)
			emu_attach_mouse_cb(dev, NULL);
	}
}

static void rumble_write(int pad, int low, int high)
{
	maple_device_t *dev;
	unsigned int i;
	purupuru_effect_t effect = { 0 };

	if (pad < 0 || pad >= 4)
		return;

	if (bloom_rumble_should_run(1, low, high)) {
		effect.cont = true;
		effect.motor = 1;
		effect.fpow = low ? 1 : (uint8_t)high >> 5;
		effect.freq = 21;
		effect.inc = 38;
	}

	for (i = 0; i < MAPLE_UNIT_COUNT; i++) {
		dev = maple_enum_dev(pad, i);

		if (dev && (dev->info.functions & MAPLE_FUNC_PURUPURU)) {
			purupuru_rumble(dev, &effect);
			return;
		}
	}
}

void input_apply_settings(void)
{
	unsigned int i;

	for (i = 0; i < BLOOM_PAD_COUNT; i++) {
		if (in_type[i] == PSE_PAD_TYPE_ANALOGPAD
		    || in_type[i] == PSE_PAD_TYPE_STANDARD) {
			in_type[i] = bloom_settings_analog()
				? PSE_PAD_TYPE_ANALOGPAD : PSE_PAD_TYPE_STANDARD;
		}
	}

	if (!bloom_settings_rumble()) {
		for (i = 0; i < 4; i++)
			rumble_write((int)i, 0, 0);
	}
}

void input_shutdown(void) {
	unsigned int i;

	for (i = 0; i < 4; i++)
		rumble_write((int)i, 0, 0);

	maple_attach_callback(MAPLE_FUNC_CONTROLLER, NULL, NULL);
	maple_detach_callback(MAPLE_FUNC_CONTROLLER, NULL, NULL);
	maple_attach_callback(MAPLE_FUNC_MOUSE, NULL, NULL);
	maple_detach_callback(MAPLE_FUNC_MOUSE, NULL, NULL);
}

long PAD__open(void)
{
	return PSE_PAD_ERR_SUCCESS;
}

long PAD__close(void) {
	return PSE_PAD_ERR_SUCCESS;
}

static long reportMouse(maple_device_t *dev, PadDataS *pad)
{
	mouse_state_t *state = (mouse_state_t *)maple_dev_status(dev);
	uint16_t buttons = 0;

	if (state->buttons & MOUSE_RIGHTBUTTON)
		buttons |= BIT(10);
	if (state->buttons & MOUSE_LEFTBUTTON)
		buttons |= BIT(11);

	pad->moveX = state->dx;
	pad->moveY = state->dy;
	pad->buttonStatus = ~buttons;

	return 0;
}

long PAD1_readPort(PadDataS *pad) {
	unsigned int idx = pad->requestPadIndex;
	maple_device_t *dev;
	cont_state_t *state;
	uint16_t buttons = 0;
	uint8_t joyx, joyy, joy2x, joy2y;
	int start_held;

	if (idx >= BLOOM_PAD_COUNT) {
		pad->controllerType = PSE_PAD_TYPE_NONE;
		return 0;
	}

	pad->controllerType = in_type[idx];
	if (pad->controllerType == PSE_PAD_TYPE_NONE)
		return 0;

	dev = maple_enum_dev(idx, 0);
	if (!dev)
		return 0;

	pad->portMultitap = bloom_pad_wants_multitap(idx, use_multitap);

	if (dev->info.functions & MAPLE_FUNC_MOUSE)
		return reportMouse(dev, pad);

	if (!(dev->info.functions & MAPLE_FUNC_CONTROLLER))
		return 0;

	state = (cont_state_t *)maple_dev_status(dev);
	start_held = !!(state->buttons & CONT_START);

	if (state->buttons & CONT_Z)
		buttons |= BIT(DKEY_SELECT);
	if (state->buttons & CONT_DPAD2_LEFT)
		buttons |= BIT(DKEY_L3);
	if (state->buttons & CONT_DPAD2_DOWN)
		buttons |= BIT(DKEY_R3);
	buttons |= bloom_start_buttons(start_held, idx, &start_mask,
				       &old_start_mask, &combo_mask, DKEY_START);
	if (state->buttons & CONT_DPAD_UP)
		buttons |= BIT(DKEY_UP);
	if (state->buttons & CONT_DPAD_RIGHT)
		buttons |= BIT(DKEY_RIGHT);
	if (state->buttons & CONT_DPAD_DOWN)
		buttons |= BIT(DKEY_DOWN);
	if (state->buttons & CONT_DPAD_LEFT)
		buttons |= BIT(DKEY_LEFT);
	if (state->buttons & CONT_C)
		buttons |= BIT(DKEY_L2);
	if (state->buttons & CONT_D)
		buttons |= BIT(DKEY_R2);
	if (state->ltrig > 128)
		buttons |= bloom_button_combo(idx, start_held, DKEY_L2, DKEY_L1,
					      &combo_mask);
	if (state->rtrig > 128)
		buttons |= bloom_button_combo(idx, start_held, DKEY_R2, DKEY_R1,
					      &combo_mask);
	if (state->buttons & CONT_A)
		buttons |= bloom_button_combo(idx, start_held, DKEY_SELECT,
					      DKEY_CROSS, &combo_mask);
	if (state->buttons & CONT_B)
		buttons |= bloom_button_combo(idx, start_held, DKEY_R3,
					      DKEY_CIRCLE, &combo_mask);
	if (state->buttons & CONT_X)
		buttons |= bloom_button_combo(idx, start_held, DKEY_L3,
					      DKEY_SQUARE, &combo_mask);
	if (state->buttons & CONT_Y)
		buttons |= BIT(DKEY_TRIANGLE);

	pad->buttonStatus = ~buttons;

	if (pad->controllerType == PSE_PAD_TYPE_ANALOGPAD) {
		joyx = bloom_analog_scale(state->joyx + 128);
		joyy = bloom_analog_scale(state->joyy + 128);
		joy2x = bloom_analog_scale(state->joy2x + 128);
		joy2y = bloom_analog_scale(state->joy2y + 128);

		bloom_map_analog_combo(start_held, joyx, joyy, joy2x, joy2y,
				       &combo_mask, idx,
				       &pad->leftJoyX, &pad->leftJoyY,
				       &pad->rightJoyX, &pad->rightJoyY);

		if (state->buttons & CONT_DPAD2_RIGHT)
			pad->ds.padMode ^= 1;
	}

	return 0;
}

long PAD2_readPort(PadDataS *pad) {
	return PAD1_readPort(pad);
}

void plat_trigger_vibrate(int pad, int low, int high) {
	if (!bloom_rumble_should_run(bloom_settings_rumble(), low, high)) {
		rumble_write(pad, 0, 0);
		return;
	}
	rumble_write(pad, low, high);
}

void pl_gun_byte2(int port, unsigned char byte)
{
	(void)port;
	(void)byte;
}
