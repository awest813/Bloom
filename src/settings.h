/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Persistent Bloom settings (no KallistiOS required)
 *
 * Copyright (C) 2026 Bloom contributors
 */

#ifndef BLOOM_SETTINGS_H
#define BLOOM_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

enum bloom_setting_id {
	BLOOM_SET_SILENT_AUDIO = 0,
	BLOOM_SET_RUMBLE,
	BLOOM_SET_ANALOG,
	BLOOM_SET_VIDEO_480P,
	BLOOM_SET_BILINEAR,
	BLOOM_SET_HYBRID,
	BLOOM_SET_CLIPPING,
	BLOOM_SET_FSAA,
	BLOOM_SET_COUNT
};

struct bloom_settings {
	char last_path[256];
	int silent_audio;
	int rumble;
	int analog;
	int video_480p;
	int bilinear;
	int hybrid;
	int clipping;
	int fsaa;
	int allow_480p;
	int allow_aica;
	int allow_bilinear;
	int allow_hybrid;
	int allow_clipping;
	int allow_fsaa;
};

struct bloom_settings_boot {
	int video_480p;
	int bilinear;
	int silent_audio;
	int hybrid;
	int clipping;
	int fsaa;
	int allow_480p;
	int allow_aica;
	int allow_bilinear;
	int allow_hybrid;
	int allow_clipping;
	int allow_fsaa;
};

void bloom_settings_reset(struct bloom_settings *s,
			  const struct bloom_settings_boot *boot);
void bloom_settings_init(const struct bloom_settings_boot *boot);
struct bloom_settings *bloom_settings_get(void);

int bloom_settings_parse_line(struct bloom_settings *s, const char *line);
int bloom_settings_write_to(const struct bloom_settings *s, FILE *fp);
int bloom_settings_load_from(struct bloom_settings *s, FILE *fp);

const char *bloom_settings_choose_path(int (*ok)(const char *path),
				       const char *const *paths, unsigned n);

int bloom_settings_load(void);
int bloom_settings_save(void);
int bloom_settings_flush(void);
const char *bloom_settings_path(void);

void bloom_settings_set_last_path(const char *path);
int bloom_settings_cycle(enum bloom_setting_id id);
void bloom_settings_line(enum bloom_setting_id id, char *dst, size_t dst_sz);

int bloom_want_silent_audio(void);
int bloom_settings_bilinear(void);
int bloom_settings_rumble(void);
int bloom_settings_analog(void);
int bloom_settings_video_480p(void);
int bloom_settings_hybrid(void);
int bloom_settings_clipping(void);
int bloom_settings_fsaa(void);

#ifdef __cplusplus
}
#endif

#endif /* BLOOM_SETTINGS_H */
