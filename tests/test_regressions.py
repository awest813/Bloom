#!/usr/bin/env python3
"""Host checks for portable routines, with hardware calls replaced by stubs.

Compile the full audio driver and extract the VMU/PVR routines so these checks need
neither KOS headers nor a copy of the implementation. This is not a Dreamcast
integration test.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
_HOST_CC = None


def _match_pair(source, open_idx, open_ch, close_ch):
    depth = 0
    i = open_idx
    while i < len(source):
        ch = source[i]
        if ch == open_ch:
            depth += 1
        elif ch == close_ch:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def extract_function(source, name):
    """Return one C function definition, matching braces across lines."""
    pattern = re.compile(r"(?m)^[^\n]*\b" + re.escape(name) + r"\s*\(")
    for match in pattern.finditer(source):
        line_start = source.rfind("\n", 0, match.start()) + 1
        if "(" in source[line_start:match.start()]:
            continue
        paren = match.end() - 1
        close_paren = _match_pair(source, paren, "(", ")")
        if close_paren < 0:
            continue
        i = close_paren + 1
        while i < len(source) and source[i] in " \t\r\n":
            i += 1
        if i >= len(source) or source[i] != "{":
            continue
        close = _match_pair(source, i, "{", "}")
        if close < 0:
            continue
        return source[line_start:close + 1]
    raise AssertionError("Missing production function: " + name)


def functions(path, names):
    source = (ROOT / path).read_text()
    return "\n".join(extract_function(source, name) for name in names)


def host_cc():
    """Prefer $CC, then gcc, then clang, using the first that links ASan/UBSan."""
    global _HOST_CC
    if _HOST_CC:
        return _HOST_CC

    ordered = []
    env_cc = os.environ.get("CC")
    if env_cc:
        ordered.append(env_cc)
    for candidate in ("gcc", "clang"):
        if candidate not in ordered and shutil.which(candidate):
            ordered.append(candidate)

    errors = []
    for compiler in ordered:
        with tempfile.TemporaryDirectory() as directory:
            src = Path(directory) / "probe.c"
            out = Path(directory) / "probe"
            src.write_text("int main(void) { return 0; }\n")
            probe = subprocess.run(
                [compiler, "-fsanitize=address,undefined", str(src), "-o", str(out)],
                capture_output=True, text=True)
            if probe.returncode == 0:
                _HOST_CC = compiler
                return compiler
            errors.append("%s: %s" % (compiler, (probe.stderr or probe.stdout).strip()))

    raise AssertionError(
        "No compiler with address/undefined sanitizers. Tried:\n" + "\n".join(errors))


class RegressionTests(unittest.TestCase):
    def test_pvr_texture_update_edges(self):
        self.compile_and_run(r'''
#include <assert.h>
#include <stdint.h>
#define BIT(n) (1u << (n))
''' + functions("src/pvr.c", ["get_block_mask", "vram_update_block_mask"]) + r'''
int main(void) {
    /* Check every single-pixel update, including page/block edges. */
    for (unsigned y = 0; y < 512; y++)
        for (unsigned x = 0; x < 1024; x++) {
            uint64_t expected = UINT64_C(1) << (((y % 256) / 16) * 4 + (x % 64) / 16);
            assert(vram_update_block_mask(x, x + 1, y, y + 1) == expected);
        }
    assert(vram_update_block_mask(0, 64, 0, 256) == UINT64_MAX);
    assert(vram_update_block_mask(64, 128, 256, 512) == UINT64_MAX);
    assert(vram_update_block_mask(15, 17, 15, 17) == UINT64_C(0x33));
    assert(vram_update_block_mask(16, 16, 0, 10) == 0);
    assert(vram_update_block_mask(0, 10, 16, 16) == 0);
    for (unsigned x0 = 0; x0 < 64; x0 += 7)
        for (unsigned x1 = x0 + 1; x1 <= 64; x1 += 5)
            for (unsigned y0 = 0; y0 < 256; y0 += 31)
                for (unsigned y1 = y0 + 1; y1 <= 256; y1 += 29) {
                    uint64_t expected = 0;
                    for (unsigned y = y0; y < y1; y++)
                        for (unsigned x = x0; x < x1; x++)
                            expected |= UINT64_C(1) << ((y / 16) * 4 + x / 16);
                    assert(vram_update_block_mask(x0, x1, y0, y1) == expected);
                }
    return 0;
}
''')

    def test_pvr_hybrid_overflow_closes_pt_before_tr(self):
        self.compile_and_run(r'''
#include <assert.h>
''' + functions("src/pvr.c", ["pvr_hybrid_enqueue_kind"]) + r'''
int main(void) {
    /* PT, or TR after the list is already open, draws immediately. */
    assert(pvr_hybrid_enqueue_kind(0, 1, 0, 8) == 0);
    assert(pvr_hybrid_enqueue_kind(1, 0, 8, 8) == 0);
    /* TR buffers until the cap, then asks for a PT->TR flush. */
    assert(pvr_hybrid_enqueue_kind(0, 0, 0, 8) == 1);
    assert(pvr_hybrid_enqueue_kind(0, 0, 7, 8) == 1);
    assert(pvr_hybrid_enqueue_kind(0, 0, 8, 8) == 2);
    assert(pvr_hybrid_enqueue_kind(0, 0, 0, 0) == 2);
    return 0;
}
''')

    def test_pvr_blanked_display_writes_vram_without_hardware_queues(self):
        source = (ROOT / "tests/pvr_blanking.c").read_text()
        production = functions("src/pvr.c", ["psx_coord", "sw_bbox_offscreen", "sw_draw",
                                              "sw_line", "sw_draw_lines", "process_poly"])
        self.compile_and_run(source.replace(
            "/* Production functions are inserted here by test_regressions.py. */",
            production))

    def test_extracts_real_production_functions(self):
        pvr = (ROOT / "src/pvr.c").read_text()
        hybrid = extract_function(pvr, "pvr_hybrid_enqueue_kind")
        mask = extract_function(pvr, "get_block_mask")
        self.assertIn("buffered < cap", hybrid)
        self.assertNotIn("poly_get_block_mask", mask)
        self.assertIn("return get_block_mask",
                      extract_function(pvr, "vram_update_block_mask"))
        aica = (ROOT / "src/aica_out.c").read_text()
        feed = extract_function(aica, "aica_feed")
        self.assertIn("ring_drop", feed)

    def compile_and_run(self, source, extra_sources=None):
        with tempfile.TemporaryDirectory() as directory:
            source_path = Path(directory) / "check.c"
            binary = Path(directory) / "check"
            source_path.write_text(source)
            command = [host_cc(), "-std=gnu11",
                       "-g", "-Wall", "-Wextra", "-Wno-unused-function",
                       "-fno-omit-frame-pointer",
                       "-fsanitize=address,undefined",
                       "-fno-sanitize-recover=all",
                       "-I" + str(ROOT),
                       "-I" + str(ROOT / "src"),
                       "-I" + str(ROOT / "tests/stubs"),
                       "-I" + str(ROOT / "deps/pcsx_rearmed/plugins/dfsound"),
                       str(source_path)]
            for extra in extra_sources or []:
                command.append(str(extra))
            command.extend(["-o", str(binary)])
            compiled = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0,
                             compiled.stdout + compiled.stderr)
            env = os.environ.copy()
            env.setdefault("ASAN_OPTIONS", "detect_leaks=1:abort_on_error=1")
            env.setdefault("UBSAN_OPTIONS", "print_stacktrace=1:halt_on_error=1")
            result = subprocess.run([str(binary)], capture_output=True,
                                    text=True, env=env)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_vmu_ports_and_incomplete_reads(self):
        self.compile_and_run(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define MCD_SIZE (128 * 1024)
struct maple_device { unsigned int port, unit; bool valid; };
static struct { char Mcd1[20], Mcd2[20]; } Config = { "/dev/mcd0", "/dev/mcd1" };
static char Mcd1Data[MCD_SIZE], Mcd2Data[MCD_SIZE], CdromId[] = "TEST";
static int McdDisable[2], opens, read_size = MCD_SIZE;
static bool missing, corrupt;
static void *gzopen(const char *path, const char *mode) {
    (void)path; (void)mode; opens++;
    return missing ? NULL : (void *)1;
}
static int gzread(void *hnd, void *data, unsigned int size) {
    assert(hnd && size == MCD_SIZE);
    memset(data, 0, size);
    if (!corrupt) memcpy(data, "MC", 2);
    return read_size;
}
static int gzclose(void *hnd) { assert(hnd); return 0; }
''' + functions("src/mcd.c", ["mcd_valid", "mcd_fs_hotplug_vmu"]) + r'''
int main(void) {
    struct maple_device dev = { .port = 2, .unit = 1, .valid = true };
    mcd_fs_hotplug_vmu(&dev);
    dev.port = 3; mcd_fs_hotplug_vmu(&dev);
    dev.port = 0; dev.unit = 2; mcd_fs_hotplug_vmu(&dev);
    assert(opens == 0);
    dev.unit = 1;
    for (dev.port = 0; dev.port < 2; dev.port++) {
        missing = false; corrupt = false; read_size = MCD_SIZE;
        mcd_fs_hotplug_vmu(&dev);
        assert(McdDisable[dev.port] == 0);
        read_size = 2; mcd_fs_hotplug_vmu(&dev);
        assert(McdDisable[dev.port] == 1);
        read_size = MCD_SIZE; corrupt = true; mcd_fs_hotplug_vmu(&dev);
        assert(McdDisable[dev.port] == 1);
        missing = true; mcd_fs_hotplug_vmu(&dev);
        assert(McdDisable[dev.port] == 1);
        dev.valid = false; mcd_fs_hotplug_vmu(&dev);
        assert(McdDisable[dev.port] == 1);
        dev.valid = true;
    }
    return 0;
}
''')

    def test_audio_driver_lifecycle_and_buffering(self):
        self.compile_and_run((ROOT / "tests/audio_driver.c").read_text())

    def test_save_metadata_bounds(self):
        self.compile_and_run(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef int file_t;
struct vmu_pkg {
    char desc_short[16], desc_long[32], app_id[16];
    unsigned int icon_cnt, icon_anim_speed;
    uint16_t icon_pal[16];
    uint8_t *icon_data;
};
static int headers;
static void fs_vmu_set_header(int fd, struct vmu_pkg *pkg) {
    (void)fd;
    assert(pkg->icon_cnt >= 1 && pkg->icon_cnt <= 3);
    assert(memchr(pkg->desc_long, 0, sizeof(pkg->desc_long)));
    headers++;
}
static const char jis_b2_chars[] = " ,.,. :;?!";
''' + functions("src/mcd.c", ["mcd_valid", "mcd_get_file", "bgr1555_to_argb4444",
                               "mcd_convert_icon", "shift_jis_to_ascii",
                               "mcd_set_header"]) + r'''
int main(void) {
    char title[64], output[65], card[128 * 1024] = { 'M', 'C' };
    memset(title, 'A', sizeof(title));
    shift_jis_to_ascii(output, sizeof(output), title);
    assert(strlen(output) == 64);
    char small[32];
    shift_jis_to_ascii(small, sizeof(small), title);
    assert(strlen(small) == 31);
    shift_jis_to_ascii(NULL, 0, title);
    shift_jis_to_ascii(small, 1, title);
    assert(small[0] == 0);
    title[0] = (char)0x82; title[1] = 0x60; title[2] = 0;
    shift_jis_to_ascii(small, sizeof(small), title);
    assert(strcmp(small, "A") == 0);
    mcd_set_header(0, card); /* No allocated save block. */
    assert(headers == 0);
    card[128] = 0x51;
    card[0x2000] = 'S'; card[0x2001] = 'C';
    memset(card + 0x2004, 'A', 64);
    for (int icon = 0; icon < 256; icon++) {
        card[0x2002] = icon;
        int before = headers;
        mcd_set_header(0, card);
        assert(headers - before == (icon >= 0x11 && icon <= 0x13));
    }
    return 0;
}
''')


    def test_menu_helpers(self):
        source = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "menu_util.h"
'''
        self.compile_and_run(source + r'''
int main(void) {
    char buf[64];

    assert(menu_is_cd_image_ext(".CHD", true));
    assert(!menu_is_cd_image_ext(".CHD", false));
    assert(menu_is_cd_image_ext(".Bin", true));
    assert(menu_is_cd_image_ext(".img", false));
    assert(!menu_is_cd_image_ext(".txt", true));
    assert(!menu_is_cd_image_ext("", true));

    assert(menu_is_browser_root("cd"));
    assert(menu_is_browser_root("sd"));
    assert(!menu_is_browser_root("rd"));
    assert(!menu_is_browser_root("credits"));
    assert(menu_path_allowed("/"));
    assert(menu_path_allowed("/sd/games"));
    assert(!menu_path_allowed("/rd/credits"));
    assert(!menu_path_allowed("/sd/../ide"));
    assert(!menu_path_allowed("sd/games"));
    assert(strcmp(menu_volume_label("cd"), "CD-ROM") == 0);
    assert(strcmp(menu_volume_label("ide"), "Hard drive") == 0);
    assert(strcmp(menu_volume_label("other"), "other") == 0);

    assert(strcmp(menu_cd_error_text(MENU_CD_ERR_CDR), "Could not open CD-ROM") == 0);
    assert(strcmp(menu_cd_error_text(MENU_CD_ERR_SPU), "Could not open audio") == 0);
    assert(strcmp(menu_cd_error_text(MENU_CD_ERR_GPU), "Could not open GPU") == 0);
    assert(strcmp(menu_cd_error_text(MENU_CD_ERR_NOT_PSX), "Not a PlayStation disc image") == 0);
    assert(strcmp(menu_cd_error_text(MENU_CD_ERR_NO_DISC), "No PlayStation disc detected") == 0);
    assert(menu_cd_error_from_open(0) == MENU_CD_OK);
    assert(menu_cd_error_from_open(-MENU_CD_ERR_CDR) == MENU_CD_ERR_CDR);
    assert(menu_cd_error_from_open(-MENU_CD_ERR_SPU) == MENU_CD_ERR_SPU);
    assert(menu_cd_error_from_open(-MENU_CD_ERR_GPU) == MENU_CD_ERR_GPU);
    assert(menu_cd_error_from_open(-99) == MENU_CD_ERR_PLUGIN);

    menu_truncate(buf, sizeof(buf), "short", 8);
    assert(strcmp(buf, "short") == 0);
    menu_truncate(buf, sizeof(buf), "abcdefghijk", 8);
    assert(strcmp(buf, "abcde...") == 0);
    menu_truncate(buf, 4, "abcdefghijk", 8);
    assert(strlen(buf) < 4);

    menu_format_location(buf, sizeof(buf), "/");
    assert(strcmp(buf, "Select a device") == 0);
    menu_format_location(buf, sizeof(buf), "/cd");
    assert(strcmp(buf, "CD-ROM") == 0);
    menu_format_location(buf, sizeof(buf), "/sd/games");
    assert(strcmp(buf, "SD card / games") == 0);
    menu_format_location(buf, sizeof(buf), "/ide/psx/iso");
    assert(strcmp(buf, "Hard drive / psx/iso") == 0);

    assert(menu_page_step(92, 400, 20) == 15);
    assert(menu_page_step(92, 400, 0) == 1);
    assert(menu_wrap_index(-1, 5) == 4);
    assert(menu_wrap_index(5, 5) == 0);
    assert(menu_wrap_index(2, 5) == 2);
    assert(menu_clamp_index(-3, 5) == 0);
    assert(menu_clamp_index(9, 5) == 4);
    assert(menu_clamp_index(1, 0) == 0);

    const char *names[] = {"cd", "sd", "game.bin"};
    assert(menu_find_name(names, 3, "sd") == 1);
    assert(menu_find_name(names, 3, "missing") == 0);
    return 0;
}
''', extra_sources=[ROOT / "src/menu_util.c"])

    def test_input_combos_sticks_and_multitap(self):
        self.compile_and_run(r'''
#include <assert.h>
#include "input_util.h"

int main(void) {
    uint8_t start_mask = 0, old_start = 0, combo = 0;
    uint8_t lx, ly, rx, ry;

    assert(bloom_analog_scale(128) == 128);
    assert(bloom_analog_scale(0) == 0);
    assert(bloom_analog_scale(255) == 255);
    assert(bloom_clamp8(-4) == 0 && bloom_clamp8(300) == 255);

    /* Tap START: two frames of Start, then idle. */
    assert(bloom_start_buttons(1, 0, &start_mask, &old_start, &combo, 3) == 0);
    assert(start_mask == 1 && combo == 0);
    assert(bloom_start_buttons(0, 0, &start_mask, &old_start, &combo, 3) == (1u << 3));
    assert(bloom_start_buttons(0, 0, &start_mask, &old_start, &combo, 3) == (1u << 3));
    assert(bloom_start_buttons(0, 0, &start_mask, &old_start, &combo, 3) == 0);

    start_mask = old_start = combo = 0;
    bloom_start_buttons(1, 0, &start_mask, &old_start, &combo, 3);
    assert(bloom_button_combo(0, 1, 8, 10, &combo) == (1u << 8));
    assert(combo == 1);
    assert(bloom_start_buttons(0, 0, &start_mask, &old_start, &combo, 3) == 0);

    bloom_map_analog_combo(1, 10, 200, 128, 128, &combo, 1, &lx, &ly, &rx, &ry);
    assert(lx == 128 && ly == 128 && rx == 10 && ry == 200);
    assert(combo & (1u << 1));
    bloom_map_analog_combo(0, 40, 50, 60, 70, &combo, 1, &lx, &ly, &rx, &ry);
    assert(lx == 40 && ly == 50 && rx == 60 && ry == 70);

    assert(bloom_pad_wants_multitap(0, 1));
    assert(!bloom_pad_wants_multitap(1, 1));
    assert(!bloom_pad_wants_multitap(0, 0));
    assert(bloom_rumble_should_run(1, 0, 40));
    assert(!bloom_rumble_should_run(1, 0, 0));
    assert(!bloom_rumble_should_run(0, 7, 9));
    assert(!bloom_button_combo(8, 1, 1, 2, &combo));
    return 0;
}
''', extra_sources=[ROOT / "src/input_util.c"])

    def test_settings_parse_cycle_and_paths(self):
        self.compile_and_run(r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "settings.h"

static const char *readable;

static int is_readable(const char *path) {
    return readable && strcmp(path, readable) == 0;
}

int main(void) {
    struct bloom_settings s;
    struct bloom_settings_boot boot = {
        .video_480p = 1, .allow_480p = 1, .allow_aica = 1, .allow_bilinear = 1,
        .hybrid = 1, .allow_hybrid = 1, .clipping = 1, .allow_clipping = 1,
        .fsaa = 0, .allow_fsaa = 1,
    };
    char line[80];
    const char *paths[] = { "/sd/bloom.cfg", "/ide/bloom.cfg", "/ram/bloom.cfg" };
    FILE *fp = tmpfile();
    char buf[256];

    bloom_settings_reset(&s, &boot);
    assert(s.rumble && s.analog && s.video_480p && !s.bilinear && !s.silent_audio);
    assert(s.hybrid && s.clipping && !s.fsaa);
    assert(bloom_settings_parse_line(&s, "last_path=/etc/passwd") == 1);
    assert(s.last_path[0] == 0);
    assert(bloom_settings_parse_line(&s, "last_path=/sd/games"));
    assert(bloom_settings_parse_line(&s, " silent_audio = on "));
    assert(bloom_settings_parse_line(&s, "hybrid=off"));
    assert(bloom_settings_parse_line(&s, "# comment") == 0);
    assert(s.silent_audio && !s.hybrid);
    assert(strcmp(s.last_path, "/sd/games") == 0);
    assert(bloom_settings_write_to(&s, fp) == 0);
    rewind(fp);
    bloom_settings_reset(&s, &boot);
    assert(bloom_settings_load_from(&s, fp) >= 4);
    assert(s.silent_audio && strcmp(s.last_path, "/sd/games") == 0);
    fclose(fp);

    bloom_settings_init(&boot);
    bloom_settings_set_last_path("/ide/iso");
    assert(bloom_settings_cycle(BLOOM_SET_SILENT_AUDIO));
    assert(bloom_want_silent_audio());
    bloom_settings_line(BLOOM_SET_SILENT_AUDIO, line, sizeof(line));
    assert(strstr(line, "Silent"));
    assert(bloom_settings_cycle(BLOOM_SET_RUMBLE));
    assert(!bloom_settings_rumble());
    assert(bloom_settings_cycle(BLOOM_SET_HYBRID));
    assert(!bloom_settings_hybrid());
    assert(bloom_settings_cycle(BLOOM_SET_CLIPPING));
    assert(!bloom_settings_clipping());
    assert(bloom_settings_cycle(BLOOM_SET_FSAA));
    assert(bloom_settings_fsaa());
    assert(bloom_settings_cycle(BLOOM_SET_BILINEAR));
    assert(bloom_settings_bilinear());

    struct bloom_settings_boot locked = { .silent_audio = 1 };
    bloom_settings_init(&locked);
    assert(bloom_want_silent_audio());
    assert(!bloom_settings_video_480p());
    assert(!bloom_settings_bilinear());
    assert(!bloom_settings_hybrid());
    assert(!bloom_settings_clipping());
    assert(!bloom_settings_fsaa());
    assert(!bloom_settings_cycle(BLOOM_SET_SILENT_AUDIO));
    assert(!bloom_settings_cycle(BLOOM_SET_VIDEO_480P));
    assert(!bloom_settings_cycle(BLOOM_SET_BILINEAR));
    assert(!bloom_settings_cycle(BLOOM_SET_HYBRID));
    assert(!bloom_settings_cycle(BLOOM_SET_CLIPPING));
    assert(!bloom_settings_cycle(BLOOM_SET_FSAA));

    bloom_settings_reset(&s, &locked);
    assert(bloom_settings_parse_line(&s, "hybrid=1"));
    assert(bloom_settings_parse_line(&s, "clipping=1"));
    assert(bloom_settings_parse_line(&s, "fsaa=1"));
    fp = tmpfile();
    assert(fp && bloom_settings_write_to(&s, fp) == 0);
    rewind(fp);
    bloom_settings_reset(&s, &locked);
    assert(bloom_settings_load_from(&s, fp) >= 3);
    fclose(fp);
    assert(!s.hybrid && !s.clipping && !s.fsaa);

    readable = "/ide/bloom.cfg";
    assert(strcmp(bloom_settings_choose_path(is_readable, paths, 3),
                  "/ide/bloom.cfg") == 0);
    readable = NULL;
    assert(bloom_settings_choose_path(is_readable, paths, 3) == NULL);
    (void)buf;
    return 0;
}
''', extra_sources=[ROOT / "src/settings.c", ROOT / "src/menu_util.c"])


if __name__ == "__main__":
    unittest.main()
