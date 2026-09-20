#!/usr/bin/env python3
"""Host checks for portable routines, with hardware calls replaced by stubs.

Compile the full audio driver and extract the VMU/PVR routines so these checks need
neither KOS headers nor a copy of the implementation. This is not a Dreamcast
integration test.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def functions(path, names):
    source = (ROOT / path).read_text()
    result = []
    for name in names:
        match = re.search(r"^[^\n]*\b" + name + r"\([^;]*?\n\{.*?^\}",
                          source, re.MULTILINE | re.DOTALL)
        if not match:
            raise AssertionError("Missing production function: " + name)
        result.append(match.group())
    return "\n".join(result)


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

    def test_pvr_blanked_display_writes_vram_without_hardware_queues(self):
        source = (ROOT / "tests/pvr_blanking.c").read_text()
        production = functions("src/pvr.c", ["psx_coord", "sw_bbox_offscreen", "sw_draw",
                                              "sw_line", "sw_draw_lines", "process_poly"])
        self.compile_and_run(source.replace(
            "/* Production functions are inserted here by test_regressions.py. */",
            production))

    def compile_and_run(self, source):
        with tempfile.TemporaryDirectory() as directory:
            source_path = Path(directory) / "check.c"
            binary = Path(directory) / "check"
            source_path.write_text(source)
            subprocess.run([os.environ.get("CC", "clang"), "-std=gnu11",
                            "-g", "-fsanitize=address,undefined",
                            "-fno-sanitize-recover=all",
                            "-I" + str(ROOT),
                            "-I" + str(ROOT / "tests/stubs"),
                            "-I" + str(ROOT / "deps/pcsx_rearmed/plugins/dfsound"),
                            str(source_path), "-o", str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
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


if __name__ == "__main__":
    unittest.main()
