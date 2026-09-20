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
- A real game now boots end to end in Flycast (see gap 1), but video stops
  updating partway into the intro while audio keeps playing.

Settings persist last folder, silent vs AICA output, rumble, analog, 480p,
bilinear, hybrid, clipping, and FSAA when those are compiled in. GPU plugin
(PVR vs Unai) and 24-bit framebuffer stay compile-time.

## Major gaps (attack order)

Ordered by user-visible impact, then by how invasive the change is.
Do **not** rewrite both GPU backends. Unai stays the accuracy backstop;
PVR stays the speed path.

### 1. Video freezes mid-FMV while audio keeps running

**Why first:** this is the one blocker with fresh, reproducible evidence, and
it stops any title with an intro movie before gameplay is reachable.

Observed 2026-09-20, Flycast 2.7 on Windows x86-64, default build (PVR, AICA,
packed OpenBIOS), Final Fantasy VI from a PS1 CHD packed onto the same disc
with `-DWITH_GAME_PATH=/cd/ff6.chd`:

- Boot is clean: black during startup, then the "Published by Square
  Electronic Arts L.L.C." screen, then the intro FMV, which plays and
  visibly advances.
- Partway into the FMV the picture stops advancing. Screen captures 60 s
  apart are pixel-identical, and Flycast's own frame counter holds steady at
  5.7 fps rather than falling to zero — so the Dreamcast side keeps
  presenting, it just presents the same content.
- Audio keeps playing throughout, so the PS1 CPU and the dfsound mix are
  still running. The stall is somewhere between the disc/FMV data path and
  what reaches `dc_vout_flip()`.

Next steps, cheapest first:

1. Run the Unai build (`build/docker/bloom-ff6-unai.cdi`, already packaged)
   against the same scene. If it also freezes, the renderer is not at fault
   and the disc/CD-streaming path is; if it does not, it is a PVR bug.
   This single run decides where the rest of the work goes.
2. Get on-target `printf` out. Serial console via
   `-config config:Debug.SerialConsoleEnabled=yes` produced nothing on
   Windows; try a macOS/Linux Flycast or dc-load. Without a log this is
   guesswork.
3. FF6's intro is a 24-bit FMV, so it takes the software path in
   `dc_vout_flip()` (`copy24()`), not the hardware one. Check the
   `frame_was_24bpp` / `dc_alloc_pvram()` / `pvr_mem_free(pvram)` pairing
   across mode changes and display-disable, which is the one place that
   bookkeeping can desync.
4. Check whether `cdra_` async reads are still completing (CHD hunk
   decompression on a 16 MiB machine is the other candidate).

**Touches:** `src/platform.c`, `src/pvr.c`, the pcsx CD path. **Risk:**
unknown until step 1 narrows it.

### 2. In-game pause and quit to menu

**Why:** you cannot leave a game without `START+A+B+X+Y` (full quit). There
is no resume, reset, or disc swap. Four of the CHDs on hand are multi-disc
(FF7, FF8, FF9), so disc swap is not hypothetical.

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

### 5. Performance (only after gap 1 is closed)

`pl_frame_limit()` is empty on purpose: sleeping while frames already miss
16.7 ms makes the emulator slower.

1. Read the VMU FPS overlay (PVR vs SH-4 busy) on one 2D and one 3D title.
2. Revisit the pcsx_rearmed bump (upstream bloom PR #7) only if BIOS boot
   is still the bottleneck.
3. Measure Lightrec code-buffer invalidation (`CODE_BUFFER_SIZE_MB`,
   `src/mmap.c`) on long sessions.
4. Add a frame limiter only when average frame time is under 16.7/20 ms.

Note when reading any Flycast number: with `Dynarec.Enabled=no` the host
emulator is itself the bottleneck. On the same FF6 FMV, Flycast managed
0.5 fps on the interpreter and 5.7 fps with its dynarec.

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
- `emu.h` declares `mcd_fs_hotplug_vmu(struct maple_device *)`, but `mcd.c`
  defines it `static` with a `void *` parameter. The declaration is unused
  and cannot be satisfied; drop it.

## Suggested follow-up sequence

1. Unai-vs-PVR run to localize the FMV freeze (gap 1, step 1).
2. Whatever that points at.
3. Pause + quit to menu.
4. One savestate slot from pause.
5. One PVR hole at a time (RTT or seams, not both).
6. Profile-guided speed work.

## Already done (do not re-do)

Runtime: Final Fantasy VI boots from a CHD on `/cd` under Flycast with the
packed OpenBIOS and reaches the intro FMV. Flycast's x86-64 dynarec runs this
build without the `bm_AddBlock` assert seen on ARM64.

Build: `.gitattributes` pins `*.sh`/`*.py`/`Dockerfile` to `eol=lf`, so a
Windows checkout no longer breaks `openbios/insert_bios.sh` (CRLF shebang →
exit 127 in the `embed-bios` step). The `dreamcast` workflow pins its
toolchain image by digest; the `15.0.0-lra` tag has already drifted from GCC
15 to GCC 17. GCC 15.1 LRA only on Lightrec; CMake fail-fast (unknown
plugins, missing KOS toolchain, bad BIOS path); MIT OpenBIOS blob with
SHA-512 check; host tests with sanitizer-capable compiler fallback and
C-aware function extraction. `docs/docker-dreamcast.md` now describes a
reproducible environment built from the CI image rather than a local SDK.

Audio: AICA `snd_stream`, prefill, drop-oldest overflow, silent fallback,
host sanitizer coverage, Flycast tone smoke.

PVR: blanked scanout → SW VRAM; GP0(02) fill width; off-screen triangles,
sprites, lines; E2 texture windows; 1:1 mirrored-sprite UV inset; hybrid
overflow closes PT then flushes TR; bilinear from Settings at list open;
draw-area restore from savestate ecmds.

Menu/Settings: persisted `bloom.cfg`; last folder; named plugin errors;
locked options as "this build"; wrap/page; rumble/analog immediate; empty
device copy; status tint reset; PT/TR and horizontal-FSAA labels.
