# Stock-clock performance work — September 24, 2026

Target: 15–19 presented FPS, using **200 MHz Flycast** until physical Dreamcast
testing is available. The target is not yet met across the measured gameplay
scenes. These results are separate from the earlier 500 MHz experiment.

## Audit and measurement changes

- The Lightrec empty-cleanup fast path now has tests covering an active
  callback racing with pause, as well as concurrent publication and draining.
- Diagnostic profiling separates first-pass interpretation and GTE geometry
  from the rest of PS1 execution. Printed `ps1` includes those subcategories;
  CSV columns are exclusive. Nested attribution and wrapped arguments/return
  values are tested.
- A completed-interval snapshot provides matching presentation and vblank
  counts. The read-only Flycast collector rejects torn/inconsistent samples
  and preserves existing capture files. The summary tool weights FPS by
  actual interval duration.
- Controller commands have acknowledgments and bounded holds. A separate
  benchmark instance can opt into `bloom_test_exclusive_input = true` to
  suppress unrelated host input. This is off by default.

## Observations before the GPU-polling candidate

Native DreamSDK GCC 15.1, Release (`-O3`, omitted frame pointers), Unai, HLE,
AICA, threaded compilation, 3 MiB code buffer, both diagnostic logging options
enabled. Flycast 2.7, 200 MHz, dynamic recompilation, audio on, frame skipping
off. No default build flags were changed.

| Observed workload | Capture sequences | Duration | Presented FPS |
| --- | --- | ---: | ---: |
| Early opening movie | 50–90 | 105.77 s | 10.78 |
| Station dialogue | 226–234 | 25.12 s | 10.03 |
| First guard battle | 266–284 | 50.35 s | 16.92 |

The user confirmed they were playing in a test window. These are observed
scene windows, not a deterministic replay or a completed controlled A/B
comparison. The station dialogue was visually identified; subsequent movement
and battle samples must not be merged into its stationary window. Other user
applications and, during parts of the run, a second Flycast instance were
active. Durations are emulated timer intervals, not host wall time.

The station window attributes 45.88% to PS1 execution excluding geometry,
26.56% to drawing, 8.31% to sound, 7.58% to geometry, 3.71% to video, and 7.95%
to other work. Interpreter fallback was zero in this window. In the battle
window, geometry rises to 16.63% and drawing to 30.27%. Profiling overhead is
included; these numbers identify priorities, not release-build acceptance.

Local evidence is in `build/validation/flycast-hw-profile/profile-01.csv` and
`manifest-01.json`. The latter records executable and disc hashes. The matching
RelWithDebInfo comparison lives in `flycast-hw-baseline`; its BIOS embedding
was disabled to match the HLE candidate. Do not infer a compiler-flag gain
until matching scenes have been compared.

The normal RelWithDebInfo build was subsequently visually confirmed at the
same station dialogue. Sequences 370–392 average 9.15 FPS over 60.30 emulated
seconds, with different music timing and concurrent host load. This is a
useful comparison observation, not a synchronized compiler A/B result. Its
same-build checkpoint is `flycast-hw-baseline/data/bloom-ff7-hw-baseline_9.state`;
saving was acknowledged, but restoration has not yet been verified.

## Next execution-path candidate

The existing GPU-status polling shortcut repeatedly samples status while
advancing to an event boundary. The candidate synchronizes pending GPU work
on the first read, then reuses the unchanged plugin status while recalculating
cycle-dependent bits for every original sample. It retains the polling
threshold, cycle steps, returned status, and interrupt stopping condition.

A sanitizer regression compares 30,000 reads against the previous loop,
including busy/field-bit transitions, interlaced status, non-GPU reads, and
cycle wraparound. This establishes selected behavioral equivalence, not an
FPS gain. All 28 host sanitizer regressions, both Lua capture checks, the two
Python summary checks, and fresh native Unai/PVR builds passed. An older PVR
build cache failed compiler identification and omitted the existing GCC 15
register-allocation workaround; a fresh build configured and linked normally.
Native gameplay
comparison is required before calling the candidate
a measured improvement.

The input-isolated candidate subsequently reached the same Barret dialogue.
Sequences 250–282 average **10.18 FPS over 85.35 emulated seconds**, with the
dialogue visually confirmed before and after sampling. Its split is 45.94%
PS1 execution excluding geometry, 26.95% drawing, 7.91% sound, 7.71% geometry,
3.75% video, and 7.75% other. Interpreter fallback is negligible. Capture and
summary are in `flycast-hw-poll/profile-01.csv` and `station-summary-01.json`.
The candidate ELF SHA-256 is
`2971e41795ecc78e40728a11e6b28ddb027e4476bb060cca4c9f21a97ffbb023`.

This does not demonstrate a meaningful gain over the earlier Release sample:
the windows differ in length, music phase, and host load. The station remains
well below 15–19 FPS. The polling candidate is behavior-tested but should not
be presented as a demonstrated whole-game speedup. A same-build station
checkpoint was saved in slot 9 and the isolated candidate paused afterward;
the user's gameplay instance was left running.

Next priority: attribute the remaining execution cost (generated code,
dispatch and I/O) before attempting a larger change, and inspect the hottest
Unai drawing paths. Geometry matters more in battle, but removing all of its
measured station cost alone would still fall short. A reusable PS1-only state
would also allow matched cross-build comparisons without replaying the movie.

The supplied developer reference guide has been checked against these
measurements and primary documentation. See the
[guide assessment](reference-guide-assessment.md) for applicable ideas,
existing implementations, and corrections to its GTE and cycle-budget claims.

See [the capture protocol](performance.md#stock-clock-diagnostic-capture) for
commands and interpretation. No physical-Dreamcast validation has occurred.

## Hardware-register attribution follow-up

The optional profiler now has a tenth, exclusive `io` bucket around the six
`psxHwRead`/`psxHwWrite` entry points. It excludes nested drawing/audio and
does not include Lightrec's surrounding dispatch, cycle synchronization, or
GPU polling loop. The CSV reader defaults to the older nine-slot layout;
new captures explicitly select ten. The summarizer supports either layout
and rejects mixed layouts.

The fresh Release/Unai/HLE diagnostic build `build/dreamsdk-hw-io` passed
the native DreamSDK build. All 28 sanitizer regressions passed; the expanded
inclusive-counter test also passed separately. Both Lua checks and all three
Python summary checks passed. Wrapper tests preserve narrow read returns,
full-width write arguments, and nested SPU attribution.

`build/validation/flycast-hw-io` is a separate input-isolated 200 MHz run.
Its manifest records the executable/disc hashes and exact snapshot address
`0x8c19623c`. Startup produced complete ten-slot intervals. Startup register
call counts are high, so the added timer overhead must be considered before
comparing FPS with the nine-slot build. No station result or speedup is yet
established at launch for this diagnostic. The previous nine-slot station
result remains 10.18 FPS.

The ten-slot run subsequently reached the same Barret station dialogue,
visually confirmed before and after sequences 366–376. These six intervals
span 30.21 emulated seconds at **9.57 FPS**. Attribution is 39.10% exclusive
PS1 execution, 25.43% drawing, 11.14% audio, 7.21% geometry, 5.47% register
handlers, 3.56% video, and 8.09% other. Fallback is negligible. The summary
is `flycast-hw-io/station-summary-01.json`. Extra I/O timing overhead and
different music/host activity prevent treating the FPS difference as a
controlled regression. Register handlers alone cannot account for the full
gap to the target; surrounding dispatch remains in PS1 execution.

A [native sprite-loop experiment](unai-sprite-experiment.md) rejected blanket
two-pixel unrolling: 8-bit sprites became slower, while the 16-bit raw kernel
became faster. A targeted SH-4 raw 16-bit candidate has now passed native pixel
comparisons, a broader size/transparency timing experiment, and all 29 host
sanitizer regressions. Its full Dreamcast build succeeded and a separate FFVII
capture has started in `flycast-hw-sprite`. The original game-level performance
target remains unverified; kernel improvements are not whole-game improvements.

The sprite-only candidate reached the same Barret dialogue, visually verified
before and after sequences 252–262. It averaged **9.88 FPS over 30.17 emulated
seconds**. Drawing took 26.13% of time, or about 26.46 ms per presentation,
versus about 26.58 ms in the earlier ten-slot diagnostic. This does not
establish a material FFVII station gain; the music phase also differs.
The summary is `flycast-hw-sprite/station-summary-01.json`, with exact build
hashes in that directory's manifest. The raw 16-bit kernel improvement must
not be reported as a whole-game improvement.

A [constant GPU-polling shortcut](gpu-constant-poll-experiment.md) is the next
candidate. It skips only provably constant samples, preserving the original
stride and interrupt overshoot, with wraparound fallback. All 29 sanitizer
regressions pass, including explicit shortcut coverage, and its full native
build succeeded. The separate `flycast-hw-skip` run has started with both
the sprite path and polling shortcut. Long constant-burst microbenchmarks
improved, but short changing bursts regressed; gameplay results are pending.
The sprite-only window subsequently closed before a checkpoint was confirmed;
its CSV and summary remain available, but no slot-9 state was verified for it.

The combined candidate's station windows were **9.72 FPS** (282–292, 30.16 s)
and **10.80 FPS** (296–306, 30.19 s), from one boot. Station dialogue was
visually confirmed before and after the collection. Sound time changed from
10.40% to 3.39%, preventing a reliable speedup claim from these windows.
Summaries are `flycast-hw-skip/station-summary-01.json` and `-02.json`.

The user then reported missing character parts, specifically Cloud's shoulders.
This is an unresolved correctness concern: do not accept the candidate on FPS
alone. `flycast-ff7-render-check` runs the earlier `hw-io` image (before the raw
sprite specialization and constant polling shortcut) for a same-scene visual
comparison. No cause or fix has been established yet. The ordered game plan
is in [game-validation-queue.md](game-validation-queue.md).

The `hw-io` comparison reached the station and also visibly shows dark gaps
around Cloud's shoulders and upper back. The issue therefore predates both
recent candidate changes; its underlying cause remains unresolved. Battle
controls in `hw-skip` worked through attack and target selection, with a
59-damage hit observed. Battle sequences 440–450 averaged 14.89 FPS over
30.29 s while waiting at Attack with guards acting. This is not the complete
FFVII target or a controlled A/B battle replay.

Calling Flycast pause inside the script's overlay hung the completed battle
instance before acknowledgement. That process was closed. Overlay pause/resume
commands were removed, and diagnostic observation also runs from vblank so
black-screen runs can be measured. Both Lua harness checks pass.
