# Bloom roadmap

Audit of `src/`, CMake, host tests, and open PRs as of 2026-09-20.
Polish already landed (or in open PRs) is summarized at the bottom.
Everything in **Major gaps** still blocks a usable everyday emulator.

## Current shape

Bloom is a KallistiOS port of pcsx_rearmed + Lightrec, with a custom PVR
path and dfsound mixed to the AICA. CPU/JIT, discs, VMU cards, pads, and
basic stereo audio are far enough along to boot titles. What is not:

- PVR still glitches (seams, hybrid, remaining GPU holes). Unai is slower
  and more accurate but needs a rebuild.
- There is **no in-game pause**, disc-swap UI, or savestate UI.
- 3D is often far from full speed (community reports ~30 fps 2D / ~10 fps 3D).
- Flycast has seen Street Fighter Alpha 3 reach the animated intro with PVR
  and built-in BIOS. Title-screen, gameplay, in-game audio, and physical
  Dreamcast remain unverified.

Settings persist last folder, silent vs AICA output, rumble, analog, 480p,
bilinear, hybrid, clipping, and FSAA when those are compiled in. GPU plugin
(PVR vs Unai) and 24-bit framebuffer stay compile-time.

### Open PRs to land first (do not re-implement)

| PR | What |
| -- | ---- |
| [#5](https://github.com/awest813/Bloom/pull/5) | Host tests: sanitizer compiler fallback, C-aware extract |
| [#6](https://github.com/awest813/Bloom/pull/6) | CMake fail-fast; Dreamcast CI with pinned KOS (green SH-4 build) |
| [#7](https://github.com/awest813/Bloom/pull/7) | Checksummed MIT OpenBIOS image for default packing |
| [#8](https://github.com/awest813/Bloom/pull/8) | Menu copy; bilinear applied when a PVR list opens |

Merge order: **#5, #7, #8, then #6** (or #6 before #8 if you want CI compiling
the bilinear fix only once). #6 and #8 both touch PVR bilinear; #8 is the
menu+PVR copy, #6 is CMake/CI plus the same bilinear placement.

## Major gaps (attack order)

Ordered by user-visible impact, then by how invasive the change is.
Do **not** rewrite both GPU backends. Unai stays the accuracy backstop;
PVR stays the speed path.

### 1. Validate a real game on Flycast, then hardware

**Why first:** later PVR and audio work is guesswork until SFA3 (or one 2D
fighter) reaches a title screen with sound.

- Rebuild with packed OpenBIOS (#7) and PVR; Flycast interpreter + serial.
- Confirm title screen, at least one round of gameplay, and AICA output.
- Diff the same scene on Unai.
- Repeat on a 16 MiB Dreamcast if a disc or SD image can be written.

**Touches:** packaging (`docs/docker-dreamcast.md`), not core code unless a
blocker shows up. **Risk:** Flycast MMU/dynarec already asserts; interpreter
is the known-good path.

### 2. In-game pause and quit to menu

**Why:** you cannot leave a game without `START+A+B+X+Y` (full quit). There
is no resume, reset, or disc swap.

- Chord that does **not** eat PS1 Start (hold START + a face button that is
  already a combo, or a new chord documented in README).
- Pause overlay: resume, reset, quit to menu. Disc swap can wait one PR.
- Freeze emu + audio poll; keep PVR presenting the last frame or a dim copy.

**Touches:** `src/input.c`, `src/emu.c`, `src/genmenu.cpp`, `src/aica_out.c`
(pause feeding). **Risk:** START combos already map Select/L3/R3; do not
steal those.

### 3. Savestates as a first-class feature

`WITH_BOOT_SSTATE` loads one file at boot only.

- Save/load from the pause menu to `/sd`, `/ide`, or `/ram`.
- Keep GPU draw-area restore (`sw_sync_ecmds`) which already exists.
- One slot is enough for the first cut.

**Touches:** `src/emu.c`, pcsx savestate path, VMU/host file I/O. **Risk:**
PVR texture cache and Lightrec code buffer must invalidate on load.

### 4. PVR correctness that still drops games

Host tests cover blanking, cache block masks, hybrid enqueue policy, and
bilinear placement. Still open in the renderer:

1. Render-to-texture / hot off-screen path (off-screen SW raster exists;
   the on-screen copy-back is the hole).
2. After hybrid PT→TR flush, if the scene still overflows, fall back to
   software for that primitive instead of dropping or mixing lists.
3. Fixed-position vertical seams (SFA3 intro); 1:1 mirrored-sprite inset
   did not remove them.
4. Titles that need hybrid **off** (MGS) — Settings already toggles this
   for the next launch; confirm on hardware.

**Touches:** `src/pvr.c` only, with Unai screenshot diffs. **Risk:** high;
keep changes local to one primitive class per PR.

### 5. Performance (only after a game is playable)

`pl_frame_limit()` is empty on purpose: sleeping while frames already miss
16.7 ms makes the emulator slower.

1. Read the VMU FPS overlay (PVR vs SH-4 busy) on one 2D and one 3D title.
2. Revisit the pcsx_rearmed bump (upstream bloom PR #7) only if BIOS boot
   is still the bottleneck.
3. Measure Lightrec code-buffer invalidation (`CODE_BUFFER_SIZE_MB`,
   `src/mmap.c`) on long sessions.
4. Add a frame limiter only when average frame time is under 16.7/20 ms.

### 6. Runtime GPU / 24-bit (hard; do not start early)

PVR vs Unai and 24-bit framebuffer still need a rebuild. Shipping both GPU
backends in one ELF is a large link and RAM cost. Prefer: keep compile-time
GPU, document it, and only dual-link if pause/savestate and PVR holes are
done.

### 7. Audio extras (after the mix is in budget)

Reverb and Gaussian interpolation are off to save SH-4. dfsound's 512 KiB
SPU image is in system RAM. Do not turn them on until overlay times say
the mix is cheap. IRQ-sensitive titles (MGS) still need Null vs AICA on
hardware.

### 8. Platform leftovers (lowest)

- Light gun: `pl_gun_byte2` is a stub (`src/input.c`).
- Keyboard as a PS1 controller.
- VMU icon animation speed (`src/mcd.c` TODO).
- Multi-partition IDE/SD and exFAT.
- True spinner for `CheckCdrom()` (needs a second thread).

## Suggested PR sequence

1. Merge #5, #7, #8, #6.
2. Flycast (then hardware) validation of SFA3 or one 2D title — bugfix PRs
   only as blockers appear.
3. Pause + quit to menu.
4. One savestate slot from pause.
5. One PVR hole at a time (RTT or seams, not both).
6. Profile-guided speed work.
7. Reverb only if the overlay says the mix is idle.

## Already done (do not re-do)

Audio: AICA `snd_stream`, prefill, drop-oldest overflow, silent fallback,
host sanitizer coverage, Flycast tone smoke.

PVR: blanked scanout → SW VRAM; GP0(02) fill width; off-screen triangles,
sprites, lines; E2 texture windows; 1:1 mirrored-sprite UV inset; hybrid
overflow closes PT then flushes TR; bilinear from Settings at list open;
draw-area restore from savestate ecmds.

Menu/Settings: persisted `bloom.cfg`; last folder; named plugin errors;
locked options as “this build”; wrap/page; rumble/analog immediate.

Build: GCC 15.1 LRA only on Lightrec; dummy.elf/KOS fail-fast and Dreamcast
CI in #6; MIT OpenBIOS blob in #7; host tests pick a sanitizer-capable CC
in #5.
