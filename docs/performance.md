# Measuring performance

Configure with `-DWITH_PERF_LOG=ON` to print Bloom's presentation rate,
SH-4 busy percentage, and last PVR render time about once per second.
FPS uses the actual elapsed interval, including slow frames. Only completed,
non-blank video output callbacks are counted; this is not the PS1 vertical
blank rate. Reports are emitted from those callbacks, so they stop when no
presentation occurs. Logging is off by default because serial output has a cost.

`PVR-last` is the time of the last rendered frame, not an interval average.
It can remain unchanged when no new rendering occurs. SH-4 busy percentage
comes from KOS idle-thread accounting; it does not distinguish emulation,
JIT compilation, audio mixing, or other CPU work. Flycast's own FPS overlay
also includes the host's cost of emulating the Dreamcast.

Software scanout (and hardware PVR's 24-bit display path) also prints
`PERF-SCANOUT` every five seconds with average wait, pixel-copy, and command
submission times in milliseconds per non-blank scanout. It includes the sample
count and last source offset, destination rectangle, texture address, and PVR
vertical clip register. An interval spanning a mode change mixes those modes;
use stable scenes for comparisons. These counters compile out when logging is
disabled, and reset when the video output opens.

## FFVII display upload checkpoint, September 21

Native DreamSDK GCC 15.1, Unai + HLE + AICA, Flycast dynamic recompiler, and
`WITH_PERF_LOG=ON` were used for both builds. The original gameplay instance and
the user's separate emulator stayed running while a third instance was used
for the sequential comparison. No host speed or emulated CPU clock was changed.

| Stable 640×480, 16-bit startup sample | Before | After |
| --- | --- | --- |
| Pixel upload | 29.49–29.53 ms | 13.73–13.83 ms |
| Presentation rate | About 11.1 FPS | About 15.2 FPS |
| Display wait | About 0.001 ms | About 0.001 ms |
| Command submission | About 0.039 ms | About 0.039 ms |

The upload now locks the store queues once per image and uses the returned
alias across rows. KOS maps two consecutive 1 MiB pages, sufficient for the
entire 1 MiB texture even when it straddles a page boundary. Keeping the
conversion function out of the larger presentation callback and unrolling the
eight packed-word conversions per queue cut the measured upload cost further.
The startup-scene FPS improvement is about 37%; it is not a whole-game or
physical-Dreamcast performance claim. Other startup phases differ in cost.

The texture began at `0xa42d5120`, with a 1 MiB page boundary about 86 rows
into the 1024-pixel-stride upload. The old row-by-row mapping produced a black
upper region in Flycast. The stable mapping restored those rows in both the
credits and opening movie. Native hardware still needs verification.
The final candidate also reached the station with the full dialogue box
visible and responsive player movement. Field presentation remained roughly
10 FPS; the faster upload has not removed the dominant gameplay bottleneck.
The subsequent 320×240 16-bit samples spent roughly 4.2 ms copying pixels,
while 24-bit movie samples generally spent about 9–10 ms. Further game-speed
work should profile PS1 execution, software rasterization, audio, and decoding
separately before changing those paths.

## Optional subsystem attribution

`-DWITH_CORE_PROFILE=ON` enables linker wrappers around Lightrec execution,
renderer command lists, GPU vertical-blank handling, SPU entry points, MDEC
DMA, and CD track reads. It reports `PROFILE` about every five seconds. It
requires the single-threaded renderer and is off by default; the ordinary
build has no wrappers or extra timer calls. This option is independent of
`WITH_PERF_LOG`.

For the native SDK, enable it in a separate diagnostic build directory:

```sh
scripts/build-dreamsdk.sh build/dreamsdk-profile \
  -DGPU_PLUGIN=Unai -DSPU_PLUGIN=AICA \
  -DWITH_PERF_LOG=ON -DWITH_CORE_PROFILE=ON
```

Keep both profiling flags equal when comparing candidates. Reconfigure with
`-DWITH_CORE_PROFILE=OFF` for the ordinary build; changing a build option does
not update an already packaged CDI or a running emulator session.

The percentages partition main-thread wall time: nested wrapped calls are
charged to the inner category instead of both caller and callee. `ps1` includes
execution, JIT dispatch, and unwrapped callbacks; it is not a pure generated-code
counter. `draw` covers renderer command lists. `video` covers GPU vertical-blank
processing excluding nested drawing. `spu` includes mixing and output work
reached through the wrapped SPU entry points. `mdec` covers movie DMA decoding,
and `disc` covers synchronous track reads. `other` is the remaining wall time.
Same-object calls may remain within the enclosing category. Worker-thread
preemption or blocking is charged to whichever main-thread category was active;
these are not per-thread CPU counters. Only this profiler's own `PROFILE`
printing is excluded from the next interval. Other logging (including `PERF`
and `PERF-SCANOUT`) and timing-call overhead remain in the measured categories.

For PVR, `do_cmd_list` can queue drawing for later processing. Work drained
by `hw_render_stop`, `renderer_flush_queues`, or `renderer_sync` may therefore
be charged to `video`, `ps1`, or another enclosing category. The PVR `draw`
percentage is not total rasterization time; do not compare it directly with
Unai's immediate command processing or use it alone to rule out drawing cost.

`vblank` counts GPU vertical-blank callbacks, including callbacks that do not
present a new image. It must not be equated with presentation FPS. The three
call counts are Lightrec, drawing, and SPU entries. Accounting starts when guest
execution begins and stops before plugin shutdown. The accounting regression
covers nesting, restart, long uptime, disabled periods, and exclusion of serial
output time.
An additional regression exercises every wrapper, checking arguments, output
parameters, signed and unsigned return values, nested drawing/audio during
video updates, and forwarding while accounting is disabled. These host tests
do not substitute for native linker and runtime validation.

The first FFVII profiling run (`bloom-ff7-core.cdi`, threaded compilation)
attributed roughly 75–94% of startup intervals to `ps1`, with less than 1%
to `spu`. Early 24-bit movie intervals attributed roughly 48–51% to `ps1`,
26–27% to `mdec`, 10–11% to `video`, and 2.5–3.5% to `spu`. These are scene
samples, not whole-game averages or native hardware measurements.

At the first station dialogue (Barret: “C'mon newcomer. Follow me.”), later
threaded-compiler samples measured roughly 9.5–10.3 presentation FPS. The
main-thread breakdown was about 53–56% `ps1`, 26–28% `draw`, 4–9% `spu`,
4% `video`, and 8% `other`; the music and character animation make individual
intervals vary. The 320×240 16-bit pixel copy took about 3.47 ms. This points
the next field-scene optimization work at PS1 execution and drawing, rather
than another large scanout rewrite. Diagnostic timer calls add overhead, and
other emulator sessions were running on the same host.

An initial comparison disabled `ENABLE_THREADED_COMPILER` only in the
`bloom-ff7-sync.cdi` diagnostic image. Both images completed the opening movie
and reached that same dialogue. Later one-second field samples overlapped:
roughly 9.1–10.8 FPS with threaded compilation, versus 9.8–10.7 FPS in the
observed synchronous-compilation intervals. The synchronous run attributed
about 51–53% to `ps1`, 27–29% to `draw`, 5–9% to `spu`, and 4–4.5% to `video`.
These were separate cold boots, with music phases not synchronized; they do
not establish a small compiler-mode gain or regression. Keep threaded
compilation enabled by default. The normal build caches were restored to
threaded compilation with core profiling disabled after this experiment.

PVR builds print `PERF-PVR` counts for texture upload calls, uploaded cache
blocks, software triangles (with flat triangles as a subset), lines, and
rectangles. Counts reset after each report and renderer initialization.
The first report includes work since initialization, and work during blanked
intervals can appear in the next report. Counts describe submitted work,
not pixels, time, or a count of all hardware-rendered primitives. They compile
out when logging is disabled.

For comparisons, keep the game, BIOS, save state, renderer, compiler options,
and emulator settings fixed. Measure the same scene with logging configured
the same way in both builds. Confirm that workload counters actually exercise
the changed path. Repeat on hardware before drawing conclusions about native
Dreamcast performance.

## Host microbenchmarks

Build the test image as described in the README, then run from the repository
root. Each script alternates implementation order and reports five-run medians:

```sh
docker run --rm -v "$PWD:/workspace:ro" bloom-tests python3 tests/benchmark_triangles.py
docker run --rm -v "$PWD:/workspace:ro" bloom-tests python3 tests/benchmark_lines.py
docker run --rm -v "$PWD:/workspace:ro" bloom-tests python3 tests/benchmark_uploads.py
```

The scripts also run directly with Python 3 and a host GCC or Clang. They use
`$CC` when available, otherwise GCC or Clang, and compile with `-O2`.
These measurements isolate small routines; they do not predict game FPS.

| Path | Workload | What is compared |
| --- | --- | --- |
| Flat triangles | 5,000 opaque, untextured triangles, 320 × 240 bounds | Retained general interpolation versus constant-color drawing; full VRAM checksum must match |
| Lines | 128,000 generated lines | Original division formula versus incremental position/color updates; shader inputs are recorded, with rendering stubbed out |
| Texture uploads | 5,000,000 updates each at 1, 4, and 64 blocks | Original 64-bit mask scan versus set-bit/full-page dispatch; hardware transfer is stubbed out |

The benchmarks include compiler memory barriers to keep the recorded writes
in the timed workload; the triangle benchmark also changes color between draws.
The initial 2026-09-20 host runs, before the shared-edge coverage and presentation
lifetime corrections, measured:

| Path | Reference | Optimized | Ratio |
| --- | --- | --- | --- |
| Flat triangles | 0.955846 s | 0.342801 s | 2.79× |
| Line interpolation | 0.431733 s | 0.399841 s | 1.08× |
| One-block upload dispatch | 0.129466 s | 0.016155 s | 8.01× |
| Four-block upload dispatch | 0.110318 s | 0.035129 s | 3.14× |
| Full-page upload dispatch | 0.416954 s | 0.355619 s | 1.17× |

Results vary by compiler and host; no native Dreamcast or whole-game gain has
been measured.

Separate sanitizer regression checks compare complete pixel buffers for flat
triangles, every shader call for lines, and upload order/cache masks across
single bits, pairs, full pages, half pages, and generated masks. Timing tests
cover irregular intervals, output restarts, long uptimes, and logging on/off.
These checks protect behavior within the tested routines; they do not establish
PS1 hardware accuracy or title-screen/gameplay compatibility.
