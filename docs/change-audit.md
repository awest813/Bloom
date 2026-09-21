# Working-tree audit — 2026-09-20

Scope: accumulated renderer/presentation changes, software diagnostic mode,
audio polling cleanup, performance instrumentation, DreamSDK build setup, and
their regression fixtures. This is a source/build audit, not a gameplay or
listening session.

## Fixed during the audit

- The scanout texture could be overwritten while PVR was reading the previous
  frame. Wait for TA readiness followed by render completion before copying.
  Apply the same ordering before freeing the texture during a mode switch or
  shutdown. The installed KOS implementation clears TA busy when rendering
  starts, so waiting for readiness alone is insufficient.
- Closing a PVR session in 24-bit display mode called `hw_render_stop()` even
  though the hardware scene was already closed. Only stop it in hardware
  presentation mode, then wait for outstanding work before cleanup.
- Added a lifecycle regression using extracted production presentation routines
  and independently tracked TA/render states. It exercises both presentation
  paths, repeated frames, blanking, mode switches, shutdown, and reopening.
- Clarified that recorded performance numbers predate the subsequent coverage
  and presentation corrections. They are historical host microbenchmarks.

## Remaining compatibility work

Hardware-rendered pixels still lack a coherent PS1 VRAM mirror. Texture-window
split-budget fallback, broader raster accuracy, and hardware/game-scene seams
remain open in the [video plan](video-compatibility-plan.md). The software
diagnostic mode is a comparison tool, not a claim of complete GPU accuracy.

Waiting before reuse serializes access to the existing single presentation
texture. It prevents an overlap hazard but may affect frame rate. No new game
FPS or audio-quality improvement was measured in this audit.

## Validation

All 22 host regression checks pass with address/undefined-behavior sanitizers.
DreamSDK builds pass for hardware PVR, diagnostic PVR, and Unai. The existing
BIOS embedding warning remains; no new compiler warning was reported in these
builds. Logs: `build/validation/polish-{tests,native,software,unai}.log`.

## September 21 FFVII video and upload follow-up

- The Unai FFVII gameplay run exposed missing upper image rows. A separate
  Flycast instance reproduced the issue with no test overlay, ruling out that
  overlay as its cause. PVR clip bounds and destination coordinates were normal.
- Keep the store-queue alias returned by one lock across the entire upload.
  This preserves KOS's two-page mapping and removes per-row mutex/TLB changes.
  The credits and 24-bit movie subsequently displayed the missing upper rows.
- Isolate the conversion loop from presentation state and unroll its eight
  packed 16-bit conversions. The stable 640×480 startup sample increased from
  about 11.1 to 15.2 FPS; measured pixel upload dropped from 29.5 to 13.8 ms.
  See [performance.md](performance.md) for measurement limits. Movie decoding
  still runs slowly; no blanket full-game speedup is claimed.
- Add optional per-phase scanout timing, a counter regression, and a stronger
  SQ alias/lifetime check. All 24 sanitizer regressions pass; the strengthened
  scanout case was rerun after its final edit. Native Unai, hardware PVR
  (logging off), and software PVR (logging on) builds succeed.
- The original FFVII gameplay session and the user's other emulator were kept
  running throughout the comparison. The new image is
  `build/validation/bloom-ff7-video.cdi`.

## September 21 subsystem profiling follow-up

- Add opt-in, nested wall-time accounting for PS1 execution, drawing, audio,
  video presentation, movie decoding, and synchronous disc reads. It resets
  for each guest execution session and stops before teardown. Threaded
  rendering is rejected because these counters belong to the main thread.
- Verify exclusive nested accounting, long uptime, reset/stop, and exclusion
  of serial reporting time. The full 25-check sanitizer suite passed; the
  accounting case passed again after adding stop/teardown coverage.
- Native Unai and hardware PVR link with profiling enabled. Both normal
  builds also link with profiling disabled; the normal Unai ELF contains
  no profiler entry points or execution wrappers. The known embedded-BIOS
  section warning remains in the hardware build.
- FFVII reaches the station dialogue with profiling enabled. Its measured
  field workload is dominated by PS1 execution and software drawing; see
  [performance.md](performance.md) for sample ranges and timing caveats.
- Compare threaded and synchronous Lightrec compilation through the movie
  and the same station dialogue. Observed FPS ranges overlap, and music phases
  differ, so this is not evidence for changing the default. Keep threaded
  compilation enabled and restore profiling to off in ordinary build caches.
- Complete the first guard battle on the corrected full-height video image,
  advance the level-7 reward screens, and return to the station field. Preserve
  the earlier checkpoint separately before saving the post-battle field state.
  This verifies another scene transition, not completion of the starting mission.

## Profiling audit and polish

- Reviewed the recent profiling lifecycle, presentation-buffer ownership,
  scanout bounds, audio ring handling, and native build configuration. No new
  runtime defect was established in those paths during this pass.
- Move the profiler start/stop declarations into a shared header so the
  implementation and emulator caller share one interface.
- Add a full-profiler fixture covering all 14 forwarding wrappers, output
  parameters, signed/unsigned results, nested video/draw/audio accounting,
  and continued forwarding after profiling stops. All 26 sanitizer regressions
  pass, including the existing video and audio checks.
- DreamSDK hardware PVR builds with profiling on and off, and the normal Unai
  build passes. Both normal caches end with profiling off and threaded
  compilation on; the Unai ELF contains no profiling wrappers. Logs are
  `build/validation/audit-profile-{tests,native,native-off,unai}.log`.
- Clarify measurement limits: only `PROFILE` printing is excluded; other
  logging remains charged. PVR can defer command processing into other
  categories, so its `draw` percentage is not total drawing time and cannot
  be compared directly with Unai's.
- No running game instance or saved checkpoint was changed by this audit.
  This pass adds validation and clearer diagnostics, not a measured FPS gain.
