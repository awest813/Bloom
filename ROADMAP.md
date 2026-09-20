# Bloom roadmap

Audit of the current tree (`src/`, CMake options, and upstream
[pcercuei/bloom](https://github.com/pcercuei/bloom) issues) plus a plan for the
gaps that still block a usable emulator.

Polish already in this branch is listed at the bottom. Everything else below is
work that still needs to happen.

## Current shape

Bloom is a KallistiOS port of pcsx_rearmed + Lightrec, with a custom PVR GPU
path and dfsound mixed to the AICA. The CPU/JIT, CD-ROM, VMU memory cards,
input, and basic audio are far enough along to boot many titles. PVR
compatibility, runtime UX, and speed are not.

## Priority 1 — game audio

**Why first:** the README already advertised "no sound," and the old AICA
stub had empty `SPUasync` / XA / CDDA.

**Done in this branch:** `SPU_PLUGIN=AICA` (the default) is now pcsx_rearmed
dfsound mixed on the SH-4, with `src/aica_out.c` streaming stereo S16 44100
through KOS `snd_stream`. Voices, XA, CDDA, and SPU IRQs come from dfsound.
Reverb and Gaussian interpolation stay off. If the stream fails to start,
dfsound falls back to the silent `nullsnd` driver (IRQs still fire).

`SPU_PLUGIN=Null` is the same mixer with no output, still useful as a
correctness baseline.

The AICA driver now prebuffers one complete hardware buffer before playback,
reclaims partial initialization on failure, and drains pending audio before
discarding overflow. Its ring, callback buffer, and KOS scratch buffer total
48 KiB of SH-4 RAM (previously 96 KiB). Full-driver host tests cover startup,
shutdown/reopening, silent fallback, and sample order. A standalone test of
the production driver completed two stereo-tone passes in Flycast 2.7,
including shutdown and reopening; the listener confirmed both sounded clear.
PlayStation mixer integration and hardware playback, especially with sustained
slow emulation, still need verification.

Remaining:

1. Turn reverb / interpolation back on once the mix is cheap enough.
2. Budget AICA RAM vs. Lightrec if 16 MiB units still run out of SH-4 RAM
   (dfsound's 512 KiB SPU image lives in system RAM today).
3. Confirm IRQ-sensitive titles (MGS) against Null vs. AICA on hardware.

## Priority 2 — PVR renderer correctness

The display-disable overflow is fixed: while the PSX display is blanked,
primitives update VRAM in software instead of accumulating an unpresented PVR
scene. NULL-frame blanking requests now close the scene and render black.
Street Fighter Alpha 3 progressed through the QSound screen and animated
intro with PVR, built-in BIOS emulation, and Flycast's interpreter. Thin
vertical seams remain visible; gameplay and physical hardware still need
validation.

The hardware renderer is the only path that can be fast on Dreamcast, but it
is the main compatibility sink. Known holes, in the order they should be
attacked:

1. **Off-screen VRAM draws** (upstream issue
   [#10](https://github.com/pcercuei/bloom/issues/10)).
   **Done in this branch:** triangles, sprites, and **lines** whose clipped
   bbox is entirely off-screen are rasterized into `gpu.vram` and the texture
   cache is invalidated. Draw-area E3–E5 are restored from savestate ecmds.
   Remaining: render-to-texture for the hot path.
2. **GP0(E2) texture window.**
   **Done in this branch:** mask/offset decoded like Unai; applied per-pixel
   in the off-screen software path; PVR vertices use origin-relative remap
   when the half-open UV range sits in one tile; **on-screen sprites that wrap
   are split into window-sized quads**; **wrapping textured triangles/quads
   are clipped on window-tile boundaries** (capped at 64 splits).
3. **Horizontally flipped sprites** (upstream issue
   [#9](https://github.com/pcercuei/bloom/issues/9)).
   **Done in this branch:** 1:1 mirrored sprites get a sub-texel UV inset so
   the PVR does not sample the neighbouring column/row.
4. **Hybrid rendering.** Required for some effects, currently a source of
   glitches (MGS was reported to need it off).
   **Done in this branch:** a full TR poly buffer no longer drops primitives;
   overflow closes PT, flushes the buffered TR list, and keeps drawing in
   TR so punch-through order stays legal. Remaining: software fallback
   when the scene still overflows after that flush, and titles that need
   hybrid off (MGS).
5. **Savestate draw-area restore** for E3–E5.
   **Done in this branch** via `sw_sync_ecmds()`.

Keep Unai as the "it should look right" reference. Every PVR fix should be
diffed against Unai on a short game list (BIOS, Crash, Spyro, MGS, F1 2001,
Rayman, a 2D fighter).

GP0(02) fill width rounding (BIOS leftover bars) is already fixed in this
branch.

## Priority 3 — runtime UX

Almost every user-facing setting is compile-time only.

1. Add an Options screen with real toggles for renderer (PVR/Unai), 480p,
   hybrid rendering, and SPU backend, persisted to `/sd` or `/ide` when
   present, otherwise VMU/VMU-incompatible `/ram`.
   **Partly done:** Settings persist last folder, AICA vs silent output,
   rumble, analog, 480p, bilinear, hybrid rendering, clipping, and FSAA
   when those features are compiled in. PVR vs Unai still needs a rebuild.
2. In-game pause: START combo or a dedicated chord that does not eat PS1
   Start. From there: resume, reset, swap disc, save/load state, quit to
   menu.
3. Savestates as a first-class feature, not only `WITH_BOOT_SSTATE`.
4. Disc-image browser: remember last directory; show a spinner while
   `CheckCdrom()` runs (it can stall on bad dumps).
   **Done in this branch (session memory, not persisted to SD/IDE):** the
   browser reopens the last folder, restores the previous highlight when
   going up, and paints a "Checking…" status for one frame before
   `CheckCdrom()`. A true spinner still needs a second thread.
5. Surface plugin open failures in the menu (CD-ROM vs. GPU vs. SPU), not
   only "could not load."
   **Done in this branch:** failed plugin opens unwind cleanly and the
   status line names CD-ROM, audio, or GPU.

## Priority 4 — performance

Lightrec + PVR is still far from full speed on 3D games (community reports
~30 fps 2D / ~10 fps 3D). This is not a single bug.

1. Profile SH4 vs. PVR vs. CD-ROM thread on a handful of titles; the VMU FPS
   overlay already reports PVR and SH4 busy time.
2. Revisit the pcsx_rearmed bump (upstream PR
   [#7](https://github.com/pcercuei/bloom/pull/7)) — BIOS boot got much slower
   on the newer core. Either finish that port or cherry-pick only the needed
   core fixes.
3. Code buffer is 3 MiB by default and comes out of the SH4 RAM map in
   `mmap.c`. Measure overflow/invalidation on long sessions.
4. `pl_frame_limit()` is empty. Add a limiter only after average frame time
   is under 16.7/20 ms; until then it would just add sleep to an already-slow
   emulator.

## Priority 5 — platform completeness

Smaller, still user-visible:

- Light gun (`pl_gun_byte2` is a stub)
- Keyboard as a PS1 controller / cheat device
- VMU icon animation speed (TODO in `mcd.c`)
- Multi-partition IDE/SD and exFAT
- Stream alloc failure already falls back to silent dfsound (`nullsnd`)
- Automated build: a GitHub Action that cross-compiles against a pinned KOS
  image, even if it cannot run tests on hardware

## Suggested order of follow-up PRs

1. Options persistence + in-game pause/savestate
2. Performance pass guided by the VMU overlay and a fixed game set
3. Audio: reverb/interpolation once the SH-4 mix is in budget
4. Hybrid-render PT vs TR ordering if overflow still glitches on hardware

Do not wait on a full rewrite of either GPU path. Unai stays the accuracy
backstop; PVR stays the speed path; audio should not be blocked on either.

## Polish included with this audit

- GP0(02) fill width now rounds up from 10 bits, matching Unai / PSX
- `emu_check_cd(NULL)` no longer calls `strstr` on a null path (Run CD-ROM)
- File browser lists `.bin` / `.img` / `.mdf` and is case-insensitive
- Failed disc/image loads show an on-screen error instead of doing nothing
- Build info screen shows compile-time flags and the controller map
- Menu recovery for unreadable folders and credits; bounded navigation and
  consistent credit line spacing
- Audio buffering preserves stereo pairs on overflow and fills underruns
  with silence
- VMU loading rejects unsupported ports and incomplete files; metadata title
  lengths, save-block indices, and icon counts are bounded
- Menu chrome: title, location, hints, smear shadows, left-aligned
  browser, volume names, paging with L/R, wrap-around, last folder
- Settings screen: AICA vs silent SPU output, rumble, analog, 480p, bilinear,
  hybrid/clipping/FSAA when compiled in, last folder; saved to `/sd`, `/ide`,
  or `/ram`
- Checking a disc paints a status line for a frame before `CheckCdrom()`
- Plugin open failures name CD-ROM vs audio vs GPU and close what opened
- Hybrid PVR overflow now closes PT and flushes TR instead of mixing lists;
  bilinear filtering follows Settings rather than the compile-time flag
- Host regression checks cover audio buffering and VMU loading/metadata;
  sanitizer suites pass in Docker, including PVR display blanking, hybrid
  enqueue policy, texture-cache update boundaries, menu path/error helpers,
  and settings
- Full Dreamcast build passes with GCC 15.1 and current KOS; compiler
  workarounds and the installed SDK are documented in `docs/docker-dreamcast.md`
- Flycast reads the Street Fighter Alpha 3 CHD and identifies `SLUS00821`;
  PVR with built-in BIOS emulation now reaches the animated intro. Title-screen,
  gameplay/audio, and physical Dreamcast validation remain open
- README documents audio, renderer limits, controls, and CMake knobs
- Off-screen triangles/sprites/**lines** rasterize into VRAM (BIOS, F1 2001)
- GP0(E2) texture windows applied per-pixel off-screen, origin-relative at
  PVR vertices when the UV range does not wrap, by tiling on-screen sprites,
  and by clipping wrapping triangles/quads on window-tile boundaries
- Hybrid TR poly buffer overflow no longer drops primitives
- 1:1 mirrored sprites get a sub-texel UV inset (Hercules/Rayman garbage column)
- Savestate replay restores the GPU draw area for the off-screen path
- Default AICA plugin streams dfsound's mix (voices, XA, CDDA, SPU IRQs)
