// SPDX-License-Identifier: GPL-2.0-only
/*
 * Portable menu helpers (no KallistiOS)
 *
 * Copyright (C) 2026 Bloom contributors
 */

#include "menu_util.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static void ascii_lower_copy(char *dst, size_t dst_sz, const char *src)
{
	size_t i;

	if (!dst_sz)
		return;

	for (i = 0; src && src[i] && i + 1 < dst_sz; i++)
		dst[i] = (char)tolower((unsigned char)src[i]);
	dst[i] = '\0';
}

bool menu_is_cd_image_ext(const char *ext, bool with_chd)
{
	char e[16];

	ascii_lower_copy(e, sizeof(e), ext ? ext : "");

	return !strcmp(e, ".iso") || !strcmp(e, ".cue") || !strcmp(e, ".ccd")
		|| !strcmp(e, ".exe") || !strcmp(e, ".mds") || !strcmp(e, ".pbp")
		|| !strcmp(e, ".bin") || !strcmp(e, ".img") || !strcmp(e, ".mdf")
		|| (with_chd && !strcmp(e, ".chd"));
}

bool menu_is_browser_root(const char *name)
{
	return name && (!strcmp(name, "cd") || !strcmp(name, "pc")
			|| !strcmp(name, "ide") || !strcmp(name, "sd"));
}

bool menu_path_allowed(const char *path)
{
	const char *p, *slash;
	char first[8];
	size_t n;

	if (!path || path[0] != '/')
		return false;

	for (p = path; *p; p++) {
		if (p[0] == '.' && p[1] == '.' &&
		    (p[2] == '/' || p[2] == '\0') &&
		    (p == path || p[-1] == '/'))
			return false;
	}

	if (!path[1])
		return true;

	p = path + 1;
	slash = strchr(p, '/');
	n = slash ? (size_t)(slash - p) : strlen(p);
	if (n == 0 || n >= sizeof(first))
		return false;
	memcpy(first, p, n);
	first[n] = '\0';
	return menu_is_browser_root(first);
}

const char *menu_volume_label(const char *name)
{
	if (!name)
		return "";
	if (!strcmp(name, "cd"))
		return "CD-ROM";
	if (!strcmp(name, "pc"))
		return "PC (dc-load)";
	if (!strcmp(name, "ide"))
		return "Hard drive";
	if (!strcmp(name, "sd"))
		return "SD card";
	return name;
}

const char *menu_cd_error_text(int code)
{
	switch (code) {
	case MENU_CD_OK:
		return "";
	case MENU_CD_ERR_CDR:
		return "Could not open CD-ROM";
	case MENU_CD_ERR_SPU:
		return "Could not open audio";
	case MENU_CD_ERR_GPU:
		return "Could not open GPU";
	case MENU_CD_ERR_PLUGIN:
		return "Could not open plugins";
	case MENU_CD_ERR_NOT_PSX:
		return "Not a PlayStation disc image";
	case MENU_CD_ERR_NO_DISC:
		return "No PlayStation disc detected";
	default:
		return "Could not load this image";
	}
}

int menu_cd_error_from_open(int ret)
{
	if (ret >= 0)
		return MENU_CD_OK;

	ret = -ret;
	switch (ret) {
	case MENU_CD_ERR_CDR:
	case MENU_CD_ERR_SPU:
	case MENU_CD_ERR_GPU:
	case MENU_CD_ERR_PLUGIN:
		return ret;
	default:
		return MENU_CD_ERR_PLUGIN;
	}
}

void menu_truncate(char *dst, size_t dst_sz, const char *src, size_t max_chars)
{
	size_t len;

	if (!dst || !dst_sz)
		return;
	if (!src)
		src = "";

	len = strlen(src);
	if (max_chars + 1 < dst_sz)
		dst_sz = max_chars + 1;

	if (len + 1 <= dst_sz) {
		memcpy(dst, src, len + 1);
		return;
	}

	if (dst_sz <= 4) {
		snprintf(dst, dst_sz, "%s", src);
		return;
	}

	memcpy(dst, src, dst_sz - 4);
	dst[dst_sz - 4] = '.';
	dst[dst_sz - 3] = '.';
	dst[dst_sz - 2] = '.';
	dst[dst_sz - 1] = '\0';
}

void menu_format_location(char *dst, size_t dst_sz, const char *path)
{
	char buf[256];
	char *slash;
	const char *rest;
	const char *vol;

	if (!dst || !dst_sz)
		return;

	if (!path || !path[0] || !strcmp(path, "/")) {
		snprintf(dst, dst_sz, "Select a device");
		return;
	}

	snprintf(buf, sizeof(buf), "%s", path[0] == '/' ? path + 1 : path);
	slash = strchr(buf, '/');
	if (slash) {
		*slash = '\0';
		rest = slash + 1;
	} else {
		rest = "";
	}

	vol = menu_volume_label(buf);
	if (rest[0])
		snprintf(dst, dst_sz, "%s / %s", vol, rest);
	else
		snprintf(dst, dst_sz, "%s", vol);
}

unsigned int menu_page_step(unsigned int list_top, unsigned int list_bottom,
			    unsigned int font_size)
{
	unsigned int span;

	if (font_size == 0 || list_bottom <= list_top)
		return 1;

	span = (list_bottom - list_top) / font_size;
	return span ? span : 1;
}

unsigned int menu_wrap_index(int index, unsigned int count)
{
	if (count == 0)
		return 0;
	index %= (int)count;
	if (index < 0)
		index += (int)count;
	return (unsigned int)index;
}

unsigned int menu_clamp_index(int index, unsigned int count)
{
	if (count == 0)
		return 0;
	if (index < 0)
		return 0;
	if ((unsigned int)index >= count)
		return count - 1;
	return (unsigned int)index;
}

unsigned int menu_find_name(const char *const *names, unsigned int count,
			    const char *wanted)
{
	unsigned int i;

	if (!names || !wanted)
		return 0;

	for (i = 0; i < count; i++) {
		if (names[i] && !strcmp(names[i], wanted))
			return i;
	}
	return 0;
}
