# FFVII performance profile — September 24, 2026

The user specified Flycast as the target. The tested profile uses Flycast 2.7
with an emulated SH-4 clock of **500 MHz**, instead of the Dreamcast's 200 MHz.
This is the main reason for the higher gameplay rate. It is not a claim that
Bloom now reaches these rates on physical Dreamcast hardware.

## Observations

| Scene | Observation |
| --- | --- |
| Station entrance / character animation | Approximately 22–30 presentation FPS |
| Later field scene outside the reactor | 24 consecutive serial samples: 25.69–27.24 FPS, approximately 26.5 FPS mean; Flycast overlay samples around 26–28 FPS |
| Observed battle view | Flycast overlay sample of 44.2 FPS; not a sustained battle average or proof of a completed, controlled battle test |
| Opening movie | Generally around 13–17 FPS, with idle SH-4 time in the 500 MHz samples; does not meet a 20 FPS minimum |

This meets 20 FPS in the sampled gameplay scenes. It does **not** establish
20 FPS everywhere, a whole-game average, complete compatibility, or audio
quality. The movie observation suggests a presentation-cadence limit, but no
movie-frame-rate metadata was inspected to establish its precise intended
cadence. No duplicated frames or frame skipping were added.

Audit qualification: scene names were inferred from screenshots, and the
controller sidecar did not record acknowledgments. These observations establish
rendered scene rates, not a deterministic gameplay replay or independently
verified input responsiveness. Do not use the 500 MHz series as the baseline
for the subsequent 15–19 FPS physical-Dreamcast goal.

The field sample's arithmetic mean is based on rounded, approximately
one-second reports, not a separately measured long-window frame count.
Slower comparison instances were closed before the final field observations.
Other user applications remained running. The old whole-machine station
checkpoint stalled while loading, so this candidate was cold-booted.

## Local setup

- Run directory: `build/validation/flycast-clock500/`
- Executable: `build/validation/flycast-clock500/flycast.exe`
- Configuration: `build/validation/flycast-clock500/emu.cfg`
- Local launcher: `build/validation/Launch-FFVII-Performance.ps1`
- Game image: `build/validation/bloom-ff7-reaper.cdi`
- Bloom: source `19b963a` plus the empty-cleanup optimization in
  `deps/lightrec/reaper.c`; DreamSDK GCC 15.1, Unai, HLE, AICA,
  threaded compilation, performance logging on, core profiling off.
- Flycast: dynamic recompilation, `Sh4Clock = 500`, DirectX 11, 480-line
  rendering, threaded rendering and vsync on, frame skipping off, audio on.

The CDI uses the user's existing FFVII Disc 1 CHD. These binaries and game
files are local build artifacts and are not checked into the repository.
The ordinary 200 MHz Flycast configurations were preserved.

CDI SHA-256:
`a4ff60839b89c6d6735b22dbcd0dd62c88e4afeb5da145e9037f73350ae9bb90`

ELF SHA-256:
`122fcc85964a426e331518a10fb5c95eea89dcf1e6224b8b26ad815661b65fa5`

Flycast's [settings slider](https://github.com/flyinghead/flycast/blob/v2.7/core/ui/settings.cpp)
only exposes 100–300 MHz. The 500 MHz value is an experimental configuration
file setting; the [dynamic recompiler](https://github.com/flyinghead/flycast/blob/v2.7/core/hw/sh4/dyna/decoder.cpp)
scales instruction-cycle charges using that configured value. Higher clocks
can change compatibility and require checking later scenes. To return this
test profile to ordinary Dreamcast timing, close it and set `Sh4Clock = 200`
in its `emu.cfg` before restarting.

## Code change and checks

The cleanup optimization skips a mutex when Lightrec's cleanup queue is empty.
It is a small independent improvement, not the explanation for the large FPS
increase. See [the microbenchmark results](performance.md#empty-lightrec-cleanup-dispatch-september-24).

All 27 host sanitizer regressions passed, including concurrent cleanup-queue
publication. The native Unai executable linked and the packaged candidate
booted into FFVII. No physical-Dreamcast test was performed.
