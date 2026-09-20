// SPDX-License-Identifier: GPL-2.0-only
/*
 * Persistent Bloom settings
 *
 * Copyright (C) 2026 Bloom contributors
 */

#include "settings.h"

#include <ctype.h>
#include <string.h>

static struct bloom_settings g_settings;
static char g_path[64];
static const char *const k_default_paths[] = {
	"/sd/bloom.cfg",
	"/ide/bloom.cfg",
	"/ram/bloom.cfg",
};

static int truthy(const char *v)
{
	return !strcmp(v, "1") || !strcmp(v, "on") || !strcmp(v, "true")
		|| !strcmp(v, "yes");
}

static void strip(char *s)
{
	char *end;
	while (*s && isspace((unsigned char)*s))
		memmove(s, s + 1, strlen(s));
	end = s + strlen(s);
	while (end > s && isspace((unsigned char)end[-1]))
		*--end = '\0';
}

void bloom_settings_reset(struct bloom_settings *s, int video_480p,
			  int bilinear, int silent_audio, int allow_480p,
			  int allow_aica)
{
	memset(s, 0, sizeof(*s));
	s->rumble = 1;
	s->analog = 1;
	s->video_480p = video_480p && allow_480p;
	s->bilinear = bilinear;
	s->silent_audio = silent_audio || !allow_aica;
	s->allow_480p = allow_480p;
	s->allow_aica = allow_aica;
}

void bloom_settings_init(int video_480p, int bilinear, int silent_audio,
			 int allow_480p, int allow_aica)
{
	g_path[0] = '\0';
	bloom_settings_reset(&g_settings, video_480p, bilinear, silent_audio,
			     allow_480p, allow_aica);
	bloom_settings_load();
	if (!g_settings.allow_480p)
		g_settings.video_480p = 0;
	if (!g_settings.allow_aica)
		g_settings.silent_audio = 1;
}

struct bloom_settings *bloom_settings_get(void)
{
	return &g_settings;
}

int bloom_settings_parse_line(struct bloom_settings *s, const char *line)
{
	char buf[320];
	char *eq, *val;

	if (!s || !line)
		return 0;
	while (*line && isspace((unsigned char)*line))
		line++;
	if (*line == '\0' || *line == '#' || *line == ';')
		return 0;

	snprintf(buf, sizeof(buf), "%s", line);
	eq = strchr(buf, '=');
	if (!eq)
		return 0;
	*eq = '\0';
	val = eq + 1;
	strip(buf);
	strip(val);

	if (!strcmp(buf, "last_path")) {
		snprintf(s->last_path, sizeof(s->last_path), "%s", val);
		return 1;
	}
	if (!strcmp(buf, "silent_audio")) {
		s->silent_audio = truthy(val);
		return 1;
	}
	if (!strcmp(buf, "rumble")) {
		s->rumble = truthy(val);
		return 1;
	}
	if (!strcmp(buf, "analog")) {
		s->analog = truthy(val);
		return 1;
	}
	if (!strcmp(buf, "video_480p")) {
		s->video_480p = truthy(val);
		return 1;
	}
	if (!strcmp(buf, "bilinear")) {
		s->bilinear = truthy(val);
		return 1;
	}
	return 0;
}

int bloom_settings_write_to(const struct bloom_settings *s, FILE *fp)
{
	if (!s || !fp)
		return -1;
	return fprintf(fp,
		"last_path=%s\n"
		"silent_audio=%d\n"
		"rumble=%d\n"
		"analog=%d\n"
		"video_480p=%d\n"
		"bilinear=%d\n",
		s->last_path, s->silent_audio, s->rumble, s->analog,
		s->video_480p, s->bilinear) < 0 ? -1 : 0;
}

int bloom_settings_load_from(struct bloom_settings *s, FILE *fp)
{
	char line[320];
	int n = 0;

	if (!s || !fp)
		return 0;
	while (fgets(line, sizeof(line), fp))
		n += bloom_settings_parse_line(s, line);
	return n;
}

const char *bloom_settings_choose_path(int (*ok)(const char *path),
				       const char *const *paths, unsigned n)
{
	unsigned i;

	if (!ok || !paths)
		return NULL;
	for (i = 0; i < n; i++) {
		if (paths[i] && ok(paths[i]))
			return paths[i];
	}
	return NULL;
}

static int path_readable(const char *path)
{
	FILE *fp = fopen(path, "r");

	if (!fp)
		return 0;
	fclose(fp);
	return 1;
}

static int path_writable_dir(const char *path)
{
	FILE *fp = fopen(path, "a");

	if (!fp)
		return 0;
	fclose(fp);
	return 1;
}

int bloom_settings_load(void)
{
	const char *path;
	FILE *fp;

	path = bloom_settings_choose_path(path_readable, k_default_paths,
					  sizeof(k_default_paths) / sizeof(k_default_paths[0]));
	if (!path)
		return 0;
	fp = fopen(path, "r");
	if (!fp)
		return 0;
	snprintf(g_path, sizeof(g_path), "%s", path);
	bloom_settings_load_from(&g_settings, fp);
	fclose(fp);
	return 1;
}

int bloom_settings_save(void)
{
	const char *path = g_path[0] ? g_path : NULL;
	FILE *fp;

	if (!path)
		path = bloom_settings_choose_path(path_writable_dir, k_default_paths,
						  sizeof(k_default_paths) / sizeof(k_default_paths[0]));
	if (!path)
		return -1;
	fp = fopen(path, "w");
	if (!fp)
		return -1;
	snprintf(g_path, sizeof(g_path), "%s", path);
	if (bloom_settings_write_to(&g_settings, fp) < 0) {
		fclose(fp);
		return -1;
	}
	fclose(fp);
	return 0;
}

const char *bloom_settings_path(void)
{
	return g_path[0] ? g_path : "";
}

void bloom_settings_set_last_path(const char *path)
{
	if (!path)
		path = "";
	snprintf(g_settings.last_path, sizeof(g_settings.last_path), "%s", path);
}

int bloom_settings_cycle(enum bloom_setting_id id)
{
	switch (id) {
	case BLOOM_SET_SILENT_AUDIO:
		if (!g_settings.allow_aica)
			return 0;
		g_settings.silent_audio = !g_settings.silent_audio;
		return 1;
	case BLOOM_SET_RUMBLE:
		g_settings.rumble = !g_settings.rumble;
		return 1;
	case BLOOM_SET_ANALOG:
		g_settings.analog = !g_settings.analog;
		return 1;
	case BLOOM_SET_VIDEO_480P:
		if (!g_settings.allow_480p)
			return 0;
		g_settings.video_480p = !g_settings.video_480p;
		return 1;
	case BLOOM_SET_BILINEAR:
		g_settings.bilinear = !g_settings.bilinear;
		return 1;
	default:
		return 0;
	}
}

void bloom_settings_line(enum bloom_setting_id id, char *dst, size_t dst_sz)
{
	const char *left, *right;

	if (!dst || !dst_sz)
		return;

	switch (id) {
	case BLOOM_SET_SILENT_AUDIO:
		left = "Audio output";
		right = g_settings.silent_audio ? "Silent" : "AICA";
		if (!g_settings.allow_aica)
			right = "Silent (this build)";
		break;
	case BLOOM_SET_RUMBLE:
		left = "Rumble";
		right = g_settings.rumble ? "On" : "Off";
		break;
	case BLOOM_SET_ANALOG:
		left = "Analog stick";
		right = g_settings.analog ? "On" : "Off";
		break;
	case BLOOM_SET_VIDEO_480P:
		left = "Game resolution";
		if (!g_settings.allow_480p)
			right = "320x240 (this build)";
		else
			right = g_settings.video_480p ? "640x480" : "320x240";
		break;
	case BLOOM_SET_BILINEAR:
		left = "Bilinear filter";
		right = g_settings.bilinear ? "On" : "Off";
		break;
	default:
		left = "Setting";
		right = "";
		break;
	}
	snprintf(dst, dst_sz, "%-18s  %s", left, right);
}

int bloom_want_silent_audio(void)
{
	return g_settings.silent_audio;
}

int bloom_settings_bilinear(void)
{
	return g_settings.bilinear;
}

int bloom_settings_rumble(void)
{
	return g_settings.rumble;
}

int bloom_settings_analog(void)
{
	return g_settings.analog;
}

int bloom_settings_video_480p(void)
{
	return g_settings.video_480p && g_settings.allow_480p;
}
