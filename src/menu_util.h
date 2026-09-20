/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Portable menu helpers (no KallistiOS)
 *
 * Copyright (C) 2026 Bloom contributors
 */

#ifndef BLOOM_MENU_UTIL_H
#define BLOOM_MENU_UTIL_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum menu_cd_error {
	MENU_CD_OK = 0,
	MENU_CD_ERR_CDR = 1,
	MENU_CD_ERR_SPU = 2,
	MENU_CD_ERR_GPU = 3,
	MENU_CD_ERR_PLUGIN = 4,
	MENU_CD_ERR_NOT_PSX = 5,
	MENU_CD_ERR_NO_DISC = 6,
};

bool menu_is_cd_image_ext(const char *ext, bool with_chd);
bool menu_is_browser_root(const char *name);
bool menu_path_allowed(const char *path);
const char *menu_volume_label(const char *name);
const char *menu_cd_error_text(int code);
void menu_truncate(char *dst, size_t dst_sz, const char *src, size_t max_chars);
void menu_format_location(char *dst, size_t dst_sz, const char *path);
unsigned int menu_page_step(unsigned int list_top, unsigned int list_bottom,
			    unsigned int font_size);
unsigned int menu_wrap_index(int index, unsigned int count);
unsigned int menu_clamp_index(int index, unsigned int count);
unsigned int menu_find_name(const char *const *names, unsigned int count,
			    const char *wanted);

#ifdef __cplusplus
}
#endif

#endif /* BLOOM_MENU_UTIL_H */
