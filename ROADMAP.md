# Bloom roadmap

Audit of the current tree (`src/`, CMake options, and upstream
[pcercuei/bloom](https://github.com/pcercuei/bloom) issues) plus a plan for the
gaps that still block a usable emulator.

Polish already in this branch is listed at the bottom. Everything else below is
work that still needs to happen.

## Current shape

Bloom is a KallistiOS port of pcsx_rearmed + Lightrec, with a custom PVR GPU
path and a thin AICA SPU stub. The CPU/JIT, CD-ROM, VMU memory cards, and input
layer are far enough along to boot many titles. Audio, PVR compatibility, and
runtime UX are not.

## Priority 1 — game audio

**Why first:** the README already advertised "no sound," and the default AICA
plugin still has empty `SPUasync`, `SPUplayADPCMchannel`, `SPUplayCDDAchannel`,
ADSR, reverb, and IRQ callbacks. Users treat that as the #1 missing feature.

Suggested sequence:

1. Keep `SPU_PLUGIN=Null` (dfsound + `nullsnd`) as the correctness baseline.
   It already emulates SPU IRQs (Metal Gear Solid uses the IRQ as a clock).
2. Make Null the documented recommendation for IRQ-sensitive games until AICA
   playback exists. Consider switching the CMake default once the silent
   dfsound path is confirmed cheaper than a broken AICA default.
3. Implement real output on AICA:
   - 24 ADPCM voices with ADSR
   - XA-ADPCM (`SPUplayADPCMchannel`)
   - CDDA (`SPUplayCDDAchannel`) mixed through AICA
   - SPU IRQ and noise/reverb at least well enough for games that probe them
4. Reuse pcsx_rearmed dfsound for decode, and only replace the backend with
   AICA DMA/streaming (see also [AICAOS](https://github.com/pcercuei/AICAOS)).
   Do not start from a clean-room SPU.

Risk: AICA RAM is 2 MiB; PS1 SPU RAM is 512 KiB plus mixing buffers. Budget
voice decode vs. CDDA vs. Lightrec's code buffer carefully (especially on
unmodded 16 MiB units).

## Priority 2 — PVR renderer correctness

The hardware renderer is the only path that can be fast on Dreamcast, but it
is the main compatibility sink. Known holes, in the order they should be
attacked:

1. **Off-screen VRAM draws** (upstream issue
   [#10](https://github.com/pcercuei/bloom/issues/10)).
   BIOS boot logo and Formula One 2001 render into off-screen draw areas and
   then sample or read back that VRAM. PVR currently only draws relative to
   the visible framebuffer. Plan: software-rasterize clipped-off primitives
   into `gpu.vram` as a stopgap, then render-to-texture for the hot path.
2. **GP0(E2) texture window.** The mask/offset fields are stored and never
   applied to UVs. Several 2D games depend on this.
3. **Horizontally flipped sprites** (upstream issue
   [#9](https://github.com/pcercuei/bloom/issues/9)).
   One-pixel garbage column from sampling the neighbouring texel. Needs a UV
   inset that does not break unflipped sprites.
4. **Hybrid rendering.** Required for some effects, currently a source of
   glitches (MGS was reported to need it off). Audit the poly buffer flush
   vs. software fallback.
5. **Savestate draw-area restore** for E3–E5, otherwise the off-screen
   fallback will skip BIOS-style draws after a state load.

Keep Unai as the "it should look right" reference. Every PVR fix should be
diffed against Unai on a short game list (BIOS, Crash, Spyro, MGS, F1 2001,
Rayman, a 2D fighter).

GP0(02) fill width rounding (BIOS leftover bars) is already fixed in this
branch.

## Priority 3 — runtime UX

Almost every user-facing setting is compile-time only.

1. Turn the Options screen into real toggles for renderer (PVR/Unai), 480p,
   hybrid rendering, and SPU backend, persisted to `/sd` or `/ide` when
   present, otherwise VMU/VMU-incompatible `/ram`.
2. In-game pause: START combo or a dedicated chord that does not eat PS1
   Start. From there: resume, reset, swap disc, save/load state, quit to
   menu.
3. Savestates as a first-class feature, not only `WITH_BOOT_SSTATE`.
4. Disc-image browser: remember last directory; show a spinner while
   `CheckCdrom()` runs (it can stall on bad dumps).
5. Surface plugin open failures in the menu (CD-ROM vs. GPU vs. SPU), not
   only "could not load."

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
- Case where `snd_mem_malloc` fails on AICA init (0 is not a reliable
  failure sentinel because AICA offsets may start at zero)
- Automated build: a GitHub Action that cross-compiles against a pinned KOS
  image, even if it cannot run tests on hardware

## Suggested order of follow-up PRs

1. Audio: dfsound-on-AICA MVP (voices + XA, no reverb)
2. PVR: off-screen VRAM software fallback + GP0(E2) window
3. Options persistence + in-game pause/savestate
4. PVR: flipped-sprite seam + hybrid-render audit
5. Performance pass guided by the VMU overlay and a fixed game set

Do not wait on a full rewrite of either GPU path. Unai stays the accuracy
backstop; PVR stays the speed path; audio should not be blocked on either.

## Polish included with this audit

- GP0(02) fill width now rounds up from 10 bits, matching Unai / PSX
- `emu_check_cd(NULL)` no longer calls `strstr` on a null path (Run CD-ROM)
- File browser lists `.bin` / `.img` / `.mdf` and is case-insensitive
- Failed disc/image loads show an on-screen error instead of doing nothing
- Options screen shows compile-time flags and the controller map
- README documents audio, renderer limits, controls, and CMake knobs
- AICA plugin comments now match the register-only implementation
