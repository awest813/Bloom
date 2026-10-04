# FPS audit and implementation plan

For subsequent implementation work and 200 MHz observations, see the
[September 24 progress record](dreamcast-performance-progress.md). The audit
below retains its original September 21 evidence and proposed phases.

Audit date: September 21, 2026. Source baseline: `19b963a`.
Scope: the native DreamSDK build, FFVII's Unai/HLE/AICA measurements,
Lightrec/PCSX execution, Unai drawing, the alternative PVR backend, and the
measurement and regression infrastructure. This is a source audit and proposed
work, not a new timing run. No optimization below is a measured gain unless
explicitly described as an existing result.

## 1. Decision

Improve PS1 execution and Unai drawing together, starting with a reproducible
benchmark and finer attribution. Keep threaded compilation enabled: the
existing comparison is inconclusive, not proof that compiler scheduling is
irrelevant. Retain Unai as the working reference while advancing PVR correctness
as a separate, longer-term route to faster drawing.

The immediate deliverable should be a repeatable station/battle benchmark and
a breakdown of the current `ps1` and `draw` buckets. It should not be another
unmeasured compiler switch or a rewrite of the already-improved display upload.

## 2. What the evidence establishes

The [measurement record](performance.md) contains the scene conditions and
caveats. These values come from different observed intervals; their range
endpoints must not be added as if they were one simultaneous sample.

| Workload | Existing observation | Meaning for prioritization |
| --- | --- | --- |
| Station dialogue, Unai | Approximately 10 presented FPS; PS1 53–56%, draw 26–28%, SPU 4–9%, video about 4%, other about 8% in the recorded samples | Execution and drawing are the main field-scene targets |
| First guard battle, corrected video build | Approximately 13–18 presented FPS; both guards defeated, level 7, return to field | A second playable regression scene exists; no detailed battle CPU split yet |
| Early opening movie | PS1 about 48–51%, MDEC 26–27%, video 10–11%, SPU 2.5–3.5% | Movie work needs its own decode/convert investigation |
| Stable 640×480 startup | Upload approximately 29.5 → 13.8 ms; presentation approximately 11.1 → 15.2 FPS | Existing scanout change helped this workload, not a demonstrated whole-game gain |
| Later 320×240 field upload | Approximately 3.47 ms | Another large upload project has limited field-scene upside |
| Threaded versus synchronous compiler | Observed station ranges overlap: approximately 9.1–10.8 versus 9.8–10.7 FPS | Separate cold boots and unsynchronized music prevent a conclusion about a small difference |

The last audit passed 26 host sanitizer regressions and native Unai/PVR builds.
Those checks protect selected routines; they do not establish complete PS1
accuracy, completion of the opening mission, or physical Dreamcast performance.

### Measurement gaps that matter

- `ps1` wraps Lightrec execution. It includes dispatch, first-pass interpretation,
  recompilation reached there, geometry callbacks, and other unwrapped work.
  It does not isolate the generated MIPS code.
- The wrapper around `lightrec_run_interpreter` does not capture every fallback:
  compiled execution can internally call `lightrec_emulate_block`.
- Main-thread wall time includes preemption by the compiler and other threads.
  It is not a per-thread CPU utilization breakdown.
- Only `PROFILE` printing is excluded from its next interval. Other logging
  and timer-call overhead remain. Quantify this before chasing small gains.
- Unai processes commands immediately; PVR queues some work. PVR's `draw`
  bucket can omit work subsequently charged to `video` or `ps1`.
- Presented FPS, guest vertical blanks, guest progress, and Flycast's host
  performance are different metrics. Raising only the displayed FPS is not
  sufficient evidence that gameplay runs faster.
- Earlier comparisons kept other emulator sessions running. Preserve those
  sessions, but record host contention and do not mix those results with a
  later isolated test series without labeling the change.

## 3. Source audit findings

| Finding | Source evidence | Action |
| --- | --- | --- |
| The working FFVII image uses Unai; earlier flat-triangle/line/upload optimizations are in PVR | [CMake backend selection](../CMakeLists.txt), [PVR renderer](../src/pvr.c), current `dreamsdk-ff7` cache | Do not expect more PVR-only tuning to accelerate this Unai image |
| Lightrec is repeatedly entered up to the next guest event | `lightrec_plugin_execute_internal` in [PCSX integration](../deps/pcsx_rearmed/libpcsxcore/lightrec/plugin.c) | Measure entry frequency, guest cycles per entry, and exit reasons before changing dispatch |
| Each threaded `lightrec_execute` calls the reaper; the reaper locks even when no work is available | [Lightrec execution](../deps/lightrec/lightrec.c), [reaper](../deps/lightrec/reaper.c) | Measure empty calls and their cost; consider a synchronized empty fast path only if material |
| First-pass execution and JIT queue work occur within execution | `get_next_block_func` and [recompiler](../deps/lightrec/recompiler.c) | Separate warm execution from cold compilation and repeated invalidation |
| GTE operations call PCSX C implementations | `cop2_op` / `cp2_ops` in the integration and [GTE implementation](../deps/pcsx_rearmed/libpcsxcore/gte.c) | Measure geometry operations individually before assuming the dynarec is the entire problem |
| GPU-status polling already has a shortcut | `hw_read_word`, `GPUSTATUS_POLLING_THRESHOLD`; current threshold 350 | Audit hit rate and semantics; do not add an unbounded second shortcut |
| The port already maps PS1 RAM mirrors, BIOS, scratchpad and I/O through the MMU | [Memory mapping](../src/mmap.c) | Verify actual mapping and generated memory accesses; a generic “add fast memory” proposal duplicates existing work |
| Code buffer defaults to 3 MiB; its size moves the reserved stack/memory boundary | [CMake](../CMakeLists.txt), [memory mapping](../src/mmap.c) | Measure buffer pressure and heap/stack headroom before increasing capacity |
| Unai already specializes pixel loops by compile-time flags and simplifies some textured quads to sprites | [Command processing](../deps/pcsx_rearmed/plugins/gpu_unai/gpulib_if.cpp), [pixel drivers](../deps/pcsx_rearmed/plugins/gpu_unai/gpu_inner.h) | Measure actual primitive/driver distribution; avoid proposing specialization already present |
| Unai has ARM-specific paths; the inspected SH-4 path uses generic lighting/blending and pixel loops | `__arm__` selection in the pixel drivers | Examine generated SH-4 instructions for the hottest existing specialization |
| Audio already disables reverb, interpolation, mixing threads, tempo and XA pitch adjustments in AICA initialization | [AICA output](../src/aica_out.c) | Keep audible AICA output in acceptance runs; disabling sound is not the next optimization |
| Normal native build uses `-O2 -g -DNDEBUG`; GCC 15 LRA is deliberately restricted to Lightrec | Build cache and [CMake](../CMakeLists.txt) | Try targeted build experiments, not a global flag bundle |
| KOS uses GBR for thread-local storage; Lightrec's GBR state-pointer option is off | Installed KOS `tls_static.c`, [SH register table](../deps/lightning/lib/jit_sh.c), Lightrec configuration | Keep it off unless an ABI/TLS preservation design passes callback, interrupt and thread tests |

These are confirmed code/configuration properties. They are not proof that
each item consumes significant time in the station scene. In particular,
neither GTE nor reaper locking has yet been timed separately.

## 4. Realistic speed budgets

Use a simplified 10-FPS budget of 55% execution, 27% drawing, and 18% everything
else to decide where engineering effort could pay off. This models average
elapsed cost per presentation; it is not a measured trace of one guest frame.

`speedup = 1 / (0.55 / execution_speedup + 0.27 / drawing_speedup + 0.18)`

| Hypothetical change | Modeled resulting FPS |
| --- | ---: |
| Halve drawing time only | 11.6 |
| Halve execution time only | 13.8 |
| Reduce execution and drawing time by 20% each | 12.0 |
| Halve both execution and drawing time | 16.9 |
| Eliminate all drawing cost, leave everything else fixed | 13.7 |
| Eliminate a separate 4% video cost only | 10.4 |

These are arithmetic scenarios, not forecasts. PVR could also change CPU-side
interaction and synchronization, so the drawing-only ceiling is not a hard
bound on a future architecture. It does show why a renderer switch alone is
not a credible promise of full speed.

Use 12 FPS as an initial engineering milestone from a reproducible 10-FPS
station baseline, then reevaluate the distribution. A 20-FPS milestone needs
about half the total cost removed. Do not promise 30 or 60 FPS from current
evidence. The final target is normal guest progression and each game's actual
presentation cadence, not a universal 60 unique frames per second.

## 5. Phase A — reproducible benchmark and useful attribution

**Priority:** first. **Deliverable:** a versioned result table, scene fixtures,
and a low-overhead diagnostic breakdown. No emulator behavior changes.

### A1. Freeze a baseline

Record source revision, local dependency changes, SDK/KOS/compiler versions,
CMake cache, ELF/CDI hashes, BIOS mode, disc identity, renderer, audio settings,
region, emulated clock, host emulator settings, and concurrent test load.
Current comparison defaults are Unai, HLE, AICA, threaded compiler on,
single-threaded renderer, 3-MiB code buffer, and unchanged guest clock settings.
Keep existing runtime sessions intact and run candidates in separate instances.

Use these scenes:

| Scene | Workload and fixed endpoint | Purpose |
| --- | --- | --- |
| S0: stable startup screen | Fixed mode, bounded sampling window | Protect scanout and startup fixes |
| S1: Barret's station dialogue | Same dialogue, camera, music position and duration | Main field benchmark |
| S2: station traversal | Fixed input sequence indexed by guest frame, then battle trigger | Progression, streaming, correctness |
| S3: first guard battle | Fixed actions and targets, separate warm and transition windows | Geometry, polygons, effects, latency |
| S4: opening movie segment | Fixed decoded-frame range | MDEC and 24-bit conversion |
| S5: another available game/scene | A 2D sprite/blend workload and a differing 3D workload | Detect FFVII-only wins and regressions |

S5 remains pending until appropriate user-owned test content is available;
do not claim multi-game acceptance from FFVII alone.

### A2. Separate guest state from host emulator state

Existing Flycast `.state` files restore the entire Dreamcast, including the old
Bloom executable. They are valid checkpoints for that build, **not** inputs for
comparing new code. Never load an old full-machine checkpoint to benchmark a
different ELF/CDI.

Build a small diagnostic-only export path around PCSX `SaveState`, and use the
existing `WITH_BOOT_SSTATE` / `LoadState` path for same-core-version candidates.
First validate loading on a disposable instance: registers, RAM/VRAM, GPU state,
SPU state, disc position, JIT invalidation, output initialization and subsequent
gameplay must agree. Treat serialization and renderer compatibility as gates,
not assumptions. Keep originals and label fixture versions/hashes.

Guest states will not preserve the host JIT cache or necessarily the output-ring
phase. Define the same warm-up interval after every load, record cold behavior
separately, and establish audio synchronization before sampling. If guest-state
replay is not reliable, use a deterministic cold boot and guest-frame-indexed
inputs, with a scene/state marker to prove that comparisons reached the same
point. Current Lua holds count Dreamcast vertical blanks; identical wall-time
holds alone are insufficient when guest speed changes.

### A3. Split the broad buckets without overwhelming the CPU

- Execution: count entries, guest cycles advanced, exit reasons, block/LUT
  misses, first-pass and fallback work, recompile requests and code-buffer
  flushes. Time cold compilation separately from warmed execution.
- Geometry: aggregate per GTE operation or sample groups of calls; retain
  exact operation counts. Include math helper cost where it is measurable.
- I/O/events: count common hardware reads/writes, GPU polling shortcut hits,
  DMA/events and callback transitions. Do not print a line per event.
- Reaper/compiler: distinguish empty reaper calls, actual cleanup, queue
  depth, repeated requests and worker CPU time. Cross-thread accounting needs
  separate counters or snapshots; do not reuse the single-thread profiler's
  shared `active` category on the worker.
- Drawing: count command classes, pixel/span work, texture depths, blending,
  masking, shading, texture windows and quad-to-sprite simplification hits.
  Time command batches or sampled drivers, not every pixel.
- PVR: attribute actual queue draining, uploads, waits and CPU fallback at
  their execution sites before comparing it with Unai.

Keep counters bounded and compile them out of release paths. Measure the
profiler's overhead with matching diagnostic-on/off runs. For short hot paths,
prefer counters plus aggregated or sampled timing over two clock calls per
instruction or pixel. Preserve nested accounting and wrapper forwarding tests.

### A4. Acceptance protocol

Run at least five matched A/B pairs, alternating order, with the same warm-up
and scene duration. Start with at least 60 host-wall seconds for stable scenes;
also report elapsed time for a fixed amount of guest progress. Report median
paired change, spread, median/p95/p99 frame intervals, worst transition stall,
guest progress per host second, presentation count, audio underruns, and memory
high-water marks. Do not pool startup, movie, field and battle numbers.

Record both guest/Dreamcast timing and host-wall timing under Flycast. Accept
a claimed small improvement only when repeatability supports it. Use a 5%
end-to-end gain as an initial promotion threshold; smaller results need more
runs or clear latency/energy/maintenance value. Investigate any control-scene
regression above 2% beyond measured noise, and reject correctness regressions.
These are proposed project gates, not statistical guarantees.

## 6. Phase B — low-risk experiments, one variable at a time

**Entry gate:** Phase A can distinguish a real gain from run-to-run variation.

| Experiment | Specific action | Evidence required to retain it |
| --- | --- | --- |
| Logging cost | Compare normal instrumentation-off execution with the diagnostic setup; consider lower Lightrec log verbosity independently | Measured overhead and preserved error diagnostics; no hot-loop logging in acceptance runs |
| Targeted optimization level | Compare `-O2` and `-O3` first for Unai, then separately for selected PCSX math/Lightrec code | Inspect SH-4 output, code size, spills and cache behavior; no numerical or startup regression |
| Frame-pointer cost | Inspect compiler flags and hot functions before any target-local experiment | Benefit must justify reduced backtrace quality; keep diagnostic builds debuggable |
| Empty reaper path | If empty calls consume material time, add a synchronized no-work indication that avoids unnecessary lock entry | Prove enqueue/reap races, shutdown, invalidation and allocator pressure remain correct; never read an unprotected list as a shortcut |
| Code buffer capacity | Only if recurring flushes/recompiles are measured, test one modest size increase | Better transitions or sustained throughput with measured 16-MiB heap/stack headroom; no allocation failures |
| Memory helpers | Only if copies/fills are hot, compare existing helpers and optional `WITH_FASTMEM` | Exact overlap/alignment semantics and target measurements; do not confuse copy helpers with guest fast mapping |

Preserve the existing GCC 15 register-allocation workaround and floating-point
contraction restriction unless a separate experiment proves they can change.
LTO is a later build-system experiment because linker wrapping, fixed-register
assembly and debugging require their own validation. Do not mix it into an
optimization-level comparison.

## 7. Phase C — optimize the measured execution hotspot

Choose the branch below from Phase A data; this is a decision tree, not an
instruction to implement every change.

**Dispatch/event overhead dominates:** inspect the generated dispatcher and
the PCSX entry/exit sequence. Remove redundant host bookkeeping or repeated
lookups where invariants prove it safe. Keep interrupt deadlines, exception
delivery, cycle wrap, load/branch delays, DMA visibility and hardware side
effects unchanged. Do not enlarge execution slices past pending events merely
to reduce call count.

**Fallback/compilation dominates:** identify why blocks stay interpreted or
are repeatedly invalidated. Record the hottest guest PCs with reasons and
bounded samples. Repair an unnecessary invalidation or missed safe compilation
case only after reproducing it. Improve queue deduplication or scheduling when
queue data demonstrates a problem. Revisit synchronous compilation with the
new replay harness only if it targets the measured issue. Never suppress
self-modifying-code invalidation to manufacture a speedup.

**Generated code dominates:** inspect SH-4 instruction counts, spill/reload
traffic, helper calls, block linking and multiply/divide paths in the hot blocks.
Change one emitter/regcache pattern at a time. Preserve signedness, HI/LO,
unaligned memory operations, exceptions and register preservation. Use isolated
instruction/block tests before the full game route. Expanding generated code
can hurt instruction-cache behavior even when branch count falls.

**GTE dominates:** start with the hottest measured operation in
`gte.c` (RTPS/RTPT/MVMVA are candidates, not measured winners). Improve exact
integer scheduling, common subexpressions or a guarded SH-4 implementation.
Compare complete GTE outputs, including flags, saturation, intermediate-width
behavior, FIFOs and division edge cases, with the retained reference over
boundary and randomized inputs. A float-matrix approximation or flagless path
is a separate accuracy tradeoff, not the default FPS fix.

**Hardware polling dominates:** check whether the existing polling detector
actually fires and whether repeated callbacks are expensive. Any refined skip
must stop at the earliest observable event and preserve register side effects.
Test CD DMA, SPU IRQ, GPU busy transitions and wraparound cases. A threshold
increase by itself is not evidence of correctness.

**GBR experiment:** deferred. The register is also KOS's TLS base. A viable
design must preserve the correct value on JIT entry/exit, C callbacks, interrupts,
context switches and error paths. The existing off setting is not a harmless
missed optimization to flip on.

## 8. Phase D — accelerate Unai's actual pixel workload

**Entry gate:** primitive and driver counts identify a dominant path. Keep
the current generic path available as the differential reference.

1. Establish a replay fixture containing initial VRAM, draw state, texture
   windows, command stream and expected cycle/last-command outputs. Replay
   privately owned game-derived traces locally; use synthetic fixtures for
   checked-in tests. Compare all VRAM, not only a screenshot or visible region.
2. Select the hottest sprite/span specialization. For raw, opaque sprites,
   examine modest unrolling, register reuse, aligned paired stores and CLUT
   access scheduling. A raw texture can still contain transparent zero pixels
   and mask bits, so bulk `memcpy` is not a general substitute.
3. Guard any contiguous-row fast path by the actual texture-window mapping,
   bounds, alignment and aliasing rules. Preserve read-after-write behavior
   when source textures/CLUT and destinations share VRAM. Do not cache decoded
   colors across untracked texture or palette writes.
4. If untextured spans dominate, specialize exact fills and shaded spans
   without altering clipping, dither coordinates or interpolation. Existing
   compile-time driver flags already remove many dynamic branches.
5. Tune command unpacking/state changes only if batch timing shows meaningful
   cost. Quad-to-sprite simplification already exists; verify its hit rate
   before designing another one.
6. Try hand-written SH-4 only after C-level experiments and target disassembly
   identify a clear opportunity. Bound code growth and retain scalar handling
   for short, unaligned and tail spans.

Every candidate must preserve destination mask checks/sets, source zero
transparency, semi-transparency bit behavior, all blend modes, texture windows,
CLUT changes, clipping, signed coordinates and command ordering. Existing PVR
primitive tests do not automatically validate a new Unai fast path: add tests
against the changed production Unai routine or full replay path.

## 9. Phase E — PVR as a compatibility-gated speed path

Follow the [video compatibility plan](video-compatibility-plan.md). PVR's
potential to move drawing off the SH-4 is useful, but hardware rendering that
leaves PS1 VRAM stale is not an acceptable way to improve a benchmark.

- Establish visible/offscreen ownership and coherency for CPU readback,
  VRAM-to-VRAM copies, texture reuse, palettes, masking and state restore.
- Compare the same command replay in Unai, PVR software mode and hardware PVR.
  Use exact VRAM comparisons where a coherent representation exists and image
  comparisons for hardware output; do not silently accept known missing pixels.
- Measure the CPU cost of software replay, copy-back, texture conversion,
  synchronization and split primitives. Replaying everything may consume the
  speed gained from hardware rendering; choose scope from measured data.
- Optimize uploads, batching and fallback only after those dependencies are
  correct. Protect translucent ordering, blend semantics, shared edges and
  texture-window boundaries when batching.
- Validate 16/24-bit transitions and repeatable field/battle scenes. Keep the
  separate diagnostic software mode as a check, not a shipping speed claim.

This phase can require substantial design work. It should not block small,
proven Unai or execution improvements, nor trigger a simultaneous rewrite of
both renderers.

## 10. Separate movie, audio and presentation follow-ups

**Movies:** split MDEC into RLE/dequantization, IDCT, color conversion and DMA
bookkeeping; the current code already handles some empty/DC-only blocks.
Optimize the hottest remaining path while preserving fixed-point results and
DMA scheduling. Separately benchmark 24-bit scanout conversion, since its
9–10-ms samples are more significant than the 16-bit field upload. Do not change
`MDEC_BIAS` or delay constants as a substitute for faster host computation.

**Audio:** count mix time, polling time, underruns, dropped samples and buffer
occupancy across long scenes. Preserve voices, XA/CDDA and IRQ behavior.
Disabling AICA output is a diagnostic isolation test; it cannot qualify as a
gameplay speed improvement. A larger buffer can mask jitter but cannot repair
sustained slow emulation. Listening validation remains separate from ring tests.

**Presentation:** retain the one-lock store-queue mapping, bounded reads and
wait-before-reuse protection. Consider further conversion or buffering work
only when scene measurements justify it. Double buffering consumes scarce
memory and needs synchronization tests; the approximately 0.001-ms field wait
does not currently justify it. Never restore a texture lifetime race for FPS.

## 11. Validation, delivery and stop conditions

For each retained change:

1. State the measured hotspot, hypothesis and intended behavior preservation.
2. Add focused differential/edge-case coverage for the modified path. Run the
   full existing sanitizer suite after integration; extend it where Unai/JIT
   paths currently lack coverage.
3. Build native Unai and relevant PVR variants. Validate profiler-on/off builds
   when instrumentation changes. A successful link is not a runtime result.
4. Complete the matched scene runs, then rerun with core profiling disabled to
   establish end-to-end benefit without diagnostic overhead.
5. Play field → battle → rewards → field, check movies and audio, and run a
   proposed 30-minute soak with compilation/invalidation and memory counters.
   Eventually complete the opening mission; it is still an open acceptance item.
6. Repeat accepted candidates on a stock-memory physical Dreamcast when access
   is available. Until then label results as Flycast observations and leave the
   hardware gate open.
7. Record baseline/candidate hashes, per-scene results, code/RAM growth,
   correctness checks and limits. Promote one defensible change at a time;
   drop experiments with no repeatable benefit.

Stop and reassess if replay diverges, the gain disappears with instrumentation
off, a speedup comes from missing work or changed guest timing, memory headroom
is consumed without benefit, or compatibility regresses. Do not stack speculative
changes until it becomes impossible to explain a result.

## 12. Recommended implementation batches

| Order | Bounded batch | Exit condition | Relative effort / risk |
| --- | --- | --- | --- |
| 1 | Baseline manifest, guest-state/replay validation, scene markers and result collection | Comparable scene and guest progress across different binaries | Medium / medium |
| 2 | Execution subcategories, GTE and Unai workload counters; profiler overhead measurement | Rank actual hotspots, including cold versus warm behavior | Medium / low-to-medium |
| 3 | One targeted compiler/logging or measured empty-reaper experiment | Repeatable end-to-end gain with no correctness loss, or a documented rejection | Small-to-medium / medium |
| 4 | Hottest Unai sprite/span optimization with replay oracle | Matching VRAM and command outputs, improved scene timing | Medium / medium |
| 5 | Hottest execution/GTE/JIT optimization | Matching architectural results and better field/battle progression | Medium-to-large / high |
| 6 | Movie-specific decode/conversion improvements | Faster fixed movie segment with unchanged output/timing semantics | Medium / medium |
| 7 | PVR coherency and selected acceleration | Correct readback/texture reuse plus measured net gain | Large / high |
| 8 | Multi-game, opening-mission and physical-hardware qualification | Sustained correct gameplay with documented limits | Ongoing / depends on hardware and test content |

Effort labels describe relative scope, not calendar promises. Reorder batches
3–6 when the finer profile identifies a different dominant cost. The first two
batches are the next actionable work; later optimization targets remain gated
by their results.
