# Monster Rancher 2 performance work

October 1 user priority: sustained **19 displayed FPS with light frame skip**.
Use 200 MHz Flycast as the current hardware proxy; no physical Dreamcast
validation is claimed. Other games remain pending in game-validation-queue.md.

## Baseline

`flycast-monster-rancher-2` uses the existing Release Unai/HLE/AICA candidate,
no frame skipping, and the full ten-part profiler. Boot, title/menu, New Game,
and the opening office conversation were observed. The office's “Hello.”
dialogue measured **6.68 FPS**, sequences 198–208, 30.23 emulated seconds.
Drawing: 33.05%; PS1 exclusive: 30.09%; sound: 15.33%; geometry: 9.51%.
This includes diagnostic overhead and is not representative gameplay coverage.
Hashes and the summary are in that run directory. The baseline was closed.

## Bounded light skip candidate

`WITH_LIGHT_FRAMESKIP` defaults OFF. Opting in selects gpulib mode -2:
render three GPU frames, schedule one skip, never consecutive skips. Cadence
uses GPU flips rather than guest vblank so 30 FPS games do not alias a
60 Hz pattern. Existing display-area protection and deferred fills remain.
Force/advice cannot make this mode skip consecutively. Other modes preserve
their previous behavior. Counters describe scheduling decisions, not pixels
that were actually omitted.

`dreamsdk-mr2-light` / `flycast-mr2-light` has full subsystem profiling OFF,
AICA ON, and an independent low-overhead presentation snapshot. Its CSV records
real presentation callbacks and guest vblanks separately. Summaries report
weighted FPS, minimum interval FPS, and the proportion of intervals at least
19 FPS. Missing intervals are rejected. Boot/title rendered successfully;
the target is not achieved or claimed from menu rates.

Validation: the sanitizer suite passed 29 checks and exposed one unused test
variable in the logging-OFF fixture; that fixture was corrected and both
affected counter/policy tests pass. The new Lua capture and Python aggregation
checks pass. Native Unai build and disc packaging succeeded.

Next: compare the same office scene, then reach ranch/training/battle gameplay.
A separate PVR renderer candidate is building to investigate the drawing cost.
Reject rendering regressions rather than counting missing work as a speedup.

The low-overhead Unai light-skip office sample (480–542) is **5.98 displayed
FPS** over 33.29 s, with 15.92 guest vblanks/s and no interval reaching 19 FPS.
The scheduling counter difference is 25.29% skips (finite-window boundary);
the policy remains exactly one per four decisions. This is not a displayed
FPS improvement. The no-skip baseline carried full profiler overhead, so the
comparison also changes instrumentation. The Unai test was closed after
before/after visual verification, and the separate PVR candidate has launched.

The PVR candidate displayed the title, but after Start stayed black across
repeated visual checks while presentation counters advanced. It is rejected
for this test: its reported frame callbacks do not establish useful output.
Its capture and manifest remain in `flycast-mr2-pvr`; the instance was closed.
The software candidate restarted with fresh `frames-02.csv` and separate
`bloom-input-02.txt` to progress into gameplay. No old captures were overwritten.

## Audio cursor candidate

The no-interpolation ADPCM sampler now keeps its phase and sample cursor in
local variables between block decodes, publishing and reloading both at each
decoder callback. This preserves callback observations and mutations while
removing repeated pointer loads/stores from the per-sample loop. The reference
is generated from the existing interpolation-loop macro. 3,000 randomized and
boundary comparisons check output samples, final state, callback order/state,
and end-of-block return positions. Host ASan/UBSan and the SH-4 comparison pass;
the existing audio driver lifecycle/buffering regression also passes.

At 200 MHz Flycast, four alternating-order timing passes of 240 calls, each
generating 735 samples, gave the following isolated kernel totals:

| Pitch | Generic loop (us) | Candidate (us) | Time reduction |
| --- | ---: | ---: | ---: |
| 0x4000 | 110551 | 88679 | 19.8% |
| 0x10000 | 218609 | 175668 | 19.6% |
| 0x28000 | 408139 | 318061 | 22.1% |

These are synthetic decoder/kernel timings, **not a Monster Rancher FPS gain**.
The game remained open during this kernel comparison; the separate benchmark
was then closed. `dreamsdk-mr2-audio` is a separate build for game validation.
The running `mr2-light` game still contains the old audio loop.

`dreamsdk-mr2-pvr-noskip` includes the audio candidate and disables frame skip.
Its title/menu renders near 19 FPS, but selecting New Game leaves the old title
picture unchanged across repeated checks. Serial diagnostics report about 3.8
presentation callbacks/s, no texture uploads or software primitives, and low
SH-4 utilization. The stale on-screen 19.6 number is not gameplay performance.
This run is rejected and closed; `flycast-mr2-pvr/manifest-02.json` and
`frames-02.csv` preserve it. Frame skipping alone does not explain this failure.

## Reusable game save

The software run registered trainer A, obtained a Zuum from the market, named
it A, and saved through the game's File menu. The game displayed “Saving
finished.” The VMU backup is `build/validation/mr2-starter-city-vmu.bin`, SHA-256
`8A4B92337027927DFC853135F073B4A24FC22C027B05BB5E5791ACB576A0D388`.
Its contents were checked against the running VMU with a shared-read handle.
This is a real game save, independent of the compiled Bloom binary. The audio
candidate uses a copied `data/vmu_save_A1.bin` with `PerGameVmu = no` so changing
the generated CDI disc ID does not silently select an empty memory card.
Ranch/training/battle validation is still pending.

The working run subsequently reached the ranch, fed the starter a potato
(4990 G remaining), completed the initial Pabs dialogue, and opened the
Drill/Errantry menu. Same-build Flycast checkpoint slot 9 was written and
acknowledged (4,069,497-byte state). This checkpoint embeds the old build and
must not be restored over another candidate to compare performance.

The PVR save-loading attempt exposed a harness limitation: its picture and
overlay stopped updating, so new controller sidecar commands were no longer
consumed, although vblank diagnostics continued. Controller commands now also
poll on vblank; state save/load operations remain on overlay. Lua regression
checks cover bounded holds, duplicate suppression across both callbacks, and
state-operation exclusion from vblank. Earlier PVR failures remain failures;
unacknowledged commands cannot establish a failed guest response.

### Ranch baseline with light skip

With only the working game instance running, sequences **6812–6940** at the
Drill/Errantry menu measured **5.7746 displayed FPS** over **70.828 s**.
Guest vblanks: 15.3894/s; minimum interval: 4.7438 FPS; 0/65 intervals reached
19 FPS; skip decisions: exactly 25.0%. The camera cycles through ranch views;
the menu, date, money, and monster were visually checked before and after.
Capture: `flycast-mr2-light/ranch-summary-02.json`. This is an actual ranch
scene, but training and battle remain unmeasured. The target is not achieved.

A second real game save now starts at the ranch after the initial feeding and
Pabs visit: `build/validation/mr2-ranch-vmu.bin`, SHA-256
`3D2C7B45277938ECB83994F6B947AB4968361A7F0A8F0F184DD9257DBF982D92`.
The game confirmed completion and the backup matches the running card. Copies
are prepared for the isolated audio and PVR candidates. The baseline instance
was then closed, preserving its CSV, real save, and slot-9 checkpoint.

### Audio candidate ranch comparison

The candidate loaded the ranch VMU successfully and matched the Drill/Errantry
menu, monster, date, loyalty, style, and 4990 G. Sequences **674–800** measured
**6.1242 FPS** over **70.540 s**, with 16.3312 guest vblanks/s, minimum 5.3715
FPS, 0/64 intervals at 19 FPS, and 25.0% skip decisions. Only this game instance
ran. A separate SDK build was running on the host during part of the sample.
This is about 6.1% above the baseline's average, but the rotating camera and
music phases are not synchronized, so one comparison does not isolate a precise
audio-only game speedup. The kernel comparison remains the controlled evidence.
The 19 FPS target is still unmet. Results: `flycast-mr2-audio/ranch-summary-01.json`.

The audio candidate completed a Domino drill; pillars fell and the result
reported Pow 120 to 121. A short visually bounded animation sample (1032–1066)
was 5.7044 FPS over 20.160 s, minimum 3.5088 FPS, and no interval at 19 FPS.
This is limited training coverage, not a one-minute sustained benchmark.
Its pre-training slot-9 checkpoint was verified before closing the instance.

### Ranch subsystem and polygon profile

The separate `dreamsdk-mr2-profile` build enables the ten-part timer profiler
and timer-free polygon span counters. At the same Drill/Errantry menu,
sequences 100–124 cover 65.699 s: drawing 37.59%, PS1 exclusive 32.97%,
geometry 12.34%, sound 5.11%, I/O 3.78%, video 2.00%, other 6.20%.
The diagnostic rate is 4.67 FPS and must not be compared directly to the
uninstrumented candidate. The profile and span summaries are saved in
`flycast-mr2-profile/ranch-summary-01.json` and `ranch-spans-01.json`.
The largest polygon pixel counts are flat lit 4-bit textures (19.05 million),
flat lit 8-bit textures (14.39 million), and Gouraud lit 8-bit textures
(9.06 million). Counts precede transparent/masked pixel rejection.

The new `dreamsdk-mr2-neutral` candidate skips lighting for flat textured
polygons whose undithered 5-bit lighting channels are all 16. This extends
the existing neutral-color shortcut used for sprites and some Gouraud
polygons. Dithered polygons retain their 8-bit lighting path. A sanitizer
test checks the selector across all 24-bit colors and compares full VRAM
for 96 texture/blend/mask combinations, including texture/CLUT feedback,
transparent pixels, and opaque black. It passes; native build passes.
The candidate's ranch interval 672–798 measured **5.0629 FPS** over 75.649 s,
minimum 4.7431, no interval at 19 FPS, and 25.05% skip decisions. It rendered
the saved ranch and menu correctly, but did not demonstrate a speedup. Camera
phases differ across these short captures. The candidate is **not promoted**;
the production selector was restored and the experiment remains reproducible
in `tests/benchmark_unai_poly.py` and the archived build. Its run was closed
after verifying the scene and creating same-build checkpoint slot 9.

PVR run 4 still freezes at memory-card selection after the control harness
fix. Commands are acknowledged via vblank and guest PCs change, but the
picture and its old 18 FPS text stay unchanged while presentations are
about 3.8/s. This run is rejected and closed. A subsystem-profiled PVR build
identified the failure: KOS `pvr_state.lists_enabled` is 0x1d while
`lists_transferred` becomes 0x1f. The unexpected opaque-modifier completion
prevents KOS's equality check from starting a render. `ta_busy` remains 1,
`render_busy` is 0, and the frame never displays. Sequences 48–70 attribute
79.57% to hardware I/O (which drains PVR commands), with 3.66 callback FPS.
These are rejected stale-picture callbacks, not useful FPS. A candidate
stages modifier headers in aligned RAM before copying them into store queues;
this did **not** remove the failure. The `pvr-modhdr` candidate also reached
0x1f completions with only 0x1d enabled and froze, so it is not a confirmed fix.
Its modifier-strip fixture preserves all vertices and final-volume markers.

`WITH_PVR_TA_TRACE` is a diagnostic-only option (default OFF). It stages direct
packets in RAM and checks header list types, packed-color vertex lengths,
continuation packets, and list endings. Run 1 reproduced the freeze with no
captured format errors. This limits the diagnosis; it does not prove all
packets reaching the TA are valid, because KOS submissions and texture uploads
are outside that first tracer. Run 2 adds checks of the two SQ mapping paths.
The diagnostic fixture verifies that a 64-byte vertex continuation containing
zero is data, while an unexpected standalone zero packet is rejected.

### Flycast translation-cache fault, October 1

Trace run 3 captures actual opaque-modifier IRQ 8 during palette upload, before
the punch-through list closes. Run 4 captures the correct live SQ mapping at
that interrupt (`UTLB[62]=0x113001fc`, `UTLB[63]=0x114001fc`), with the interrupted
PC in `load_palette`. No packet-format or SQ-map check fired. A direct-VRAM
palette experiment (run 5) also failed and was reverted.

The installed proxy is Flycast `v2.7-1-g44e4c7b50`. Its `FAST_MMU` cache appends
every mapping update until its 65,536 entries are full, then silently retains
older translations. A focused sanitizer reproducer using that revision's actual
cache functions returns **0x100335c0 (TA commands)** instead of the requested
**0x114335c0 (texture upload)** after 80,004 remaps. Reusing a cached entry with
the same virtual page, ASID, and page size fixes this reproducer (two entries).
The local proxy correction is archived in
`build/validation/flycast-mmu-cache-fix.patch`; original and corrected reproducers
are beside it. `tests/check_flycast_mmu_cache.py` regenerates the focused test
from a Flycast checkout. A local MSVC Release proxy with only that cache change
loaded the same traced Bloom image past the previous card-screen stall into
the ranch, with no captured packet errors or unexpected opaque-modifier IRQ.
This is a proxy correctness correction, not a Dreamcast hardware speedup.
The failed modifier-header staging change was also reverted; its candidate
builds and fixture remain archived under `build/validation`.

### Corrected-proxy controls and light-skip diagnosis

The uninstrumented PVR no-skip control (fixed-proxy run 3) loads the ranch and
renders the animated monster and orbiting camera. At Drill/Errantry, sequences
1270–1384 cover **61.675 emulated seconds**, averaging **9.0474 presentations/s**,
minimum 7.5125, with no interval at 19. This is the stock 200 MHz proxy, AICA
audio enabled, with no concurrent build during the sample. Host playback is
slower than emulated time in this locally built proxy; these counters are not
a measurement of real Dreamcast hardware or host-wall-clock playback FPS.
The manifest, summary, and same-build slot-9 checkpoint are in
`build/validation/flycast-mr2-pvr-fixed`.

The uninstrumented light-skip run 2 produced black pictures despite advancing
counters and is rejected. An empty-scene origin-rebasing experiment (run 5)
showed the title briefly but still went black, so it was reverted. Run 4 was
interrupted after an accidental overlapping launch and is not a benchmark.

Inspection then found that `do_cmd_list_skip` copied the low halfword (UV
coordinates) instead of the high halfword (texture page) from textured polygon
packets. The fix preserves inherited texture state for later sprites. A focused
sanitizer regression checks all sixteen textured polygon command variants,
all 512 page values, and incomplete-packet handling. This test passes; game
validation is in progress as fixed-proxy run 6. It is not yet a demonstrated
performance improvement or a complete fix for PVR light skipping.

Run 6 still went black. A temporary lifecycle ring trace (run 7) showed the
actual scheduling mismatch: the generic `frame_ready` flag describes an older
software VRAM buffer, whereas PVR submits the current queued scene. It submitted
an empty scene during the skip and delayed the following drawn scene, leaving
the drawing origin behind. `renderer_notify_update_lace` now holds an empty
skipped scene, advances its coordinate origin, and presents any scene that
already contains geometry. Software PVR and 24-bit scanout retain their original
path. Run 8 with this correction rendered the title, menus, save selection, and
animated ranch successfully. Temporary lifecycle instrumentation is removed
from production source and archived under `build/validation`. All **34 host
regressions pass**, including texture-page inheritance and scene lifecycle
checks. Uninstrumented ELF/CDI `mr2-pvr-skip-fixed` are archived; a separate
subsystem profile is being collected as run 9. The 19 FPS goal remains unmet.

Run 9's ranch Drill/Errantry sequences 122–146 cover 65.374 emulated seconds:
CPU exclusive 45.46%, hardware I/O 18.42%, geometry 14.36%, other 9.62%, sound
7.68%, video 3.13%, command handling 1.32%. Diagnostic presentation rate is
7.25/s, guest vblanks 19.37/s. The monster and camera remained animated.
This is an instrumented profile, not an uninstrumented performance claim.
Per-operation GTE attribution has now been added to the optional profiler,
reusing existing timer samples rather than adding timer reads to each call.
Its wrapper, stopped-state, reset, and snapshot checks pass. Run 10 collects
those counters; production builds with profiling off do not include them.

Run 10's 65.517-second geometry sample (210–234) attributes 44.65% of geometry
time to RTPT, 14.32% to NCCT, 12.74% to RTPS, 9.45% to NCCS, 7.35% to MVMVA,
and 6.93% to NCLIP. Per-operation instrumentation adds overhead; this is for
hotspot selection, not a speed comparison.

`tests/benchmark_gte_bounds.py` compares the original 64-bit bounds checks with
a sign-extension equality check for signed 32-bit accumulator limits. All
90,112 full-register comparisons and 10,009 explicit/random 64-bit bounds cases
pass on the host with sanitizers and on the SH-4 benchmark. Existing signed
wrapping and negative shifts in the retained GTE code are handled explicitly
by the fixture's compiler flags. In the isolated 200 MHz SH-4 test, four
alternating-order runs of 4,096 operations each give typical-input RTPT
133,614 → 127,027 us (4.93% faster), NCCT 119,135 → 115,472 us (3.07%), RTPS
68,504 → 67,296 us (1.76%), NCCS 60,827 → 59,280 us (2.54%), and MVMVA
55,440 → 53,591 us (3.34%). SQR and GPF are slightly slower, so this is a
workload-dependent tradeoff, not a universal speedup. Results are in
`build/validation/flycast-gte-bounds/results-01.csv`.

The bounds candidate is implemented with an unchanged generic fallback. All
**35 regression tests pass**. Run 11's uninstrumented corrected light-skip
baseline (596–782) averaged **8.3023 presentations/s** over 100.816 emulated
seconds. Run 12 with the bounds candidate (868–1054) averaged **8.3694/s** over
101.082 seconds, minimum 7.6336, guest vblanks 22.3185/s, with exactly 25% of
eligible frames skipped and no interval reaching 19. Both samples use the
ranch Drill/Errantry menu with animated monster and camera, stock 200 MHz,
and AICA audio. There was no concurrent build during either sample. The
0.8% difference is too small to establish a game-level improvement given
unmatched camera/music phases. Complete summaries and manifests are in
`build/validation/flycast-mr2-pvr-fixed`; run 12 has a same-build slot-9 save.

The optional CPU profiler now attributes exclusive execution time to entry
PCs, recording call counts and requested cycle limits without changing guest
deadlines. Hash probing is bounded and dropped work is explicitly reported.
Wrapper tests cover nested attribution, original parameters/return values,
collisions, reset and disabled states. `tests/flycast_operation_logs.lua`
passes for both operation loggers, checking torn reads, duplicate intervals,
invalid duration/cost, observer chaining, and preservation of existing files.
Run 13 collects entry-PC diagnostics; these are not instruction-level samples
and instrumented frame rates are not performance claims. The sustained
19 useful FPS target remains unmet.

Run 13 sequences 100–124 cover 65.516 emulated seconds. Entry PCs 0x800461a0
and 0x80046158 account for 9.537 seconds, 31.89% of exclusive CPU time.
They are the two halves of a RAM-counter wait loop with a stack-resident
decrementing watchdog. The ordinary idle detector rejects its store. Table
overflow accounts for another 2.781 seconds and is retained as dropped work.
There are also 1,245,574 zero-limit single-block calls, taking 6.459 seconds,
largely HLE BIOS callbacks. These are distinct costs, not extra frame counts.

An experimental `OPT_COUNTDOWN_POLL` option (default off) recognizes the
complete executed opcode pattern without a fixed game address. It folds only
whole iterations strictly before the next event, preserves the decrementing
watchdog and temporary register, and leaves the timeout and event handling to
normal execution. It rejects custom RAM handlers, isolated RAM, misalignment,
MMIO, a store aliasing the polled word or code, changed opcodes/delay slots,
and short budgets. Stores still invalidate compiled code normally.

The sanitizer differential test passes 12,000 randomized cases, plus signature
and rejection checks. Two standalone SH-4 fixtures linked against the unchanged
and candidate Lightrec libraries match all 64 cases exactly in resulting PC,
cycle count, register/RAM hashes and exit flags. Their execution-call totals
are 6,686 versus 525 us; this is a synthetic loop comparison, not game FPS.
Source, ELFs and build script are archived under `build/validation/countdown-*`;
CSV results and comparison are under `build/validation/flycast-gte-bounds`.
The fixture requires a supplied executable code buffer, as Bloom does; the
initial fixture without that buffer stalled during initialization and is not
a result. Run 14 tests the uninstrumented candidate in the game.

Run 14 ranch sequences 492–678 average **9.9065 presentations/s** over 99.329
emulated seconds (minimum 8.6430, guest vblanks 26.4173/s, 25.019% skip
decisions, no interval at 19). This is about 18.4% above run 12's 8.3694/s.
The monster and camera remained animated, AICA stayed enabled, and no build
ran during the sample. The ELF/CDI, manifest, summary and same-build slot-9
checkpoint are archived. All **36 host regressions pass**. Further gameplay
validation is underway; the experimental option remains off by default.

The candidate reached the Domino drill's result and scolding dialogues with
intact rendering. This attempt failed the drill in-game; it is a compatibility
observation, not a training FPS measurement. The original ranch checkpoint is
retained unchanged.

Two isolated `MAC.W` geometry experiments passed 90,112 full-register checks
in each SR.S mode on SH-4, but regressed the important kernels. Typical RTPT
was 124,518 → 126,616 us in version 1 and 124,482 → 129,111 us in version 2;
NCCT was 115,179 → 116,834 and 115,185 → 118,351 us. Neither is in production.
`tests/benchmark_gte_mac.py` retains the experiment and portable reference;
the results are `gte-mac-results-01.csv` and `gte-mac-results-02.csv` in the
isolated geometry directory. The accumulator behavior was checked against
[Renesas's SH-4 software manual, section 9.55](https://www.renesas.com/en/document/mas/sh-4-software-manual).

Light skipping now has a build-time period from 4 to 60; its default remains
four. Tests cover periods 4, 8, 16 and 60, pending fills, no consecutive skips,
and unchanged legacy modes. The next candidate uses eight (render seven,
skip one). This changes the rendering schedule rather than emulation speed;
its displayed FPS must be measured separately from the one-in-four candidate.

Run 15 ranch sequences 672–858 averaged **11.7533 presentations/s** over
99.121 emulated seconds, minimum 8.6538, guest vblanks 26.8762/s, and 12.491%
skip decisions. No interval reached 19. The one-in-eight schedule improved
visible throughput over run 14 without materially changing guest throughput.
The scene remained animated, AICA was enabled, and no build ran during the
sample. The same-build slot-9 checkpoint and complete manifest are retained.
All **37 host regressions passed** before the following experiments.

Run 16 compares native 320x240 output with run 15's 480p output, retaining
one-in-eight skipping and the countdown optimization. Separately, an event
scheduler candidate combines the due-event check with the next-deadline scan
when no event is due. Due callbacks keep their original snapshot/order and
exceptions trigger a fresh deadline scan. A sanitizer differential test
matches the original two-pass behavior in 100,000 cases covering wrapping
cycle counters, callbacks changing deadlines/masks/cycles, and CPU exceptions.
Its game-level performance is still unmeasured.

Run 16 sequences 864–1050 averaged **11.1632 presentations/s** over 99.344
emulated seconds (minimum 8.3488, guest vblanks 25.5275/s, 12.490% skip
decisions, no interval at 19). Reducing output resolution did not improve this
sample. Retain 480p in the preferred configuration; run 17 uses 240p solely to
isolate the scheduler against run 16.

Timer 2's CPU/8 read now uses an unsigned shift for exactly that rate, with
the generic path retained for stopped clocks and every other rate. All
500,000 host and SH-4 differential comparisons pass, including wrapping
cycles and unusual rates. The native 100,000-read benchmark measured
37,082 → 14,041 us at rate 8; rate 1 was 13,541 → 14,045 us, stopped-clock
39,090 → 39,618 us, and rate 5 was 37,082 → 37,585 us. This trades a small
branch cost on other paths for a faster common path; game impact remains to
be measured. `tests/benchmark_timer2.py` reproduces the fixture and
`flycast-gte-bounds/timer2-results-01.csv` retains the native result.
All **39 host regressions pass** with the scheduler and timer changes.

Run 17 sequences 754–940 averaged **11.5338 presentations/s** over 98.233
emulated seconds (minimum 9.8814, guest vblanks 26.3659/s, 12.490% skip
decisions, no interval at 19). This is 3.3% above the preceding 240p sample,
but unmatched camera/music phases limit confidence in that small difference.
The run contains the event change and excludes the timer change. Run 18 is
an updated diagnostic profile with both, countdown folding and light period
eight, returning to 480p. Its FPS is not a production speed measurement.

Run 18's 65.285-second sample (210–234) attributes 35.80% to exclusive PS1
execution, 21.62% to hardware I/O, 16.62% to geometry, 9.87% to sound,
3.63% to video, 1.75% to draw and 10.71% to other work. The earlier watchdog
polling PCs no longer dominate. HLE timer-poll callback entry PCs remain
prominent. Full summaries and entry tables are retained in the run directory.

Whole-program LTO hit GCC 15.1's register allocator crash in `dc_vout_flip`.
The optional, default-off `WITH_CORE_LTO` limits LTO to the emulator libraries
and links successfully; the platform translation units retain normal
compilation. It cannot be combined with profiling because inlining can bypass
linker wrappers. The uninstrumented candidate is prepared as run 20.

The optional, default-off `WITH_SH4_UNROLLED_DIV` emits the 32 integer division
steps directly and adjusts GNU Lightning's code-size estimates. Native tests
cover signed/unsigned quotient, remainder and paired operations, output/input
aliasing, equal inputs, edge values and random operands. Both baseline and
candidate pass **246,528 comparisons**. Ordinary quotient paths take roughly
51–53 ms instead of 102–104 ms per 100,000 calls; paired operations take
55–57 ms instead of 106–111 ms. This increases generated code size and is not
yet a demonstrated game speedup.

The first division candidate failed INT_MIN/1 in Flycast's fused DIV32 path.
Inserting a NOP between DIV0 and the DIV1 sequence prevents the proxy's exact
pattern match; all native comparisons then pass using the individual SH-4
instructions. The rejected ELF is retained, and it was never used in a game
build. The native fixture is `tests/benchmark_sh4_div.c`; results are
`flycast-gte-bounds/div-baseline-01.csv` and `div-candidate-02.csv`.

Run 19, with bundled OpenBIOS, reached the title, battle demonstration and
loaded the existing ranch save. Its 98.256-second ranch sample (718–904)
averaged **13.1697 presentations/s**, minimum 11.2676, guest vblanks 30.0847/s,
12.449% skip decisions and no interval at 19. This uses 240p, light period
eight, countdown folding, the event scan and timer shift; AICA is enabled.
The same-build slot-9 checkpoint and manifest are retained. This is the best
measured ranch average so far, but the target remains unmet.

Run 20's initially black output subsequently reached a visible battle demo;
no ranch sample was collected. Run 21 (the same LTO variant plus unrolled
division) displays the title and load menu correctly. Do not interpret the
startup black-screen counters as useful FPS or as an established regression.
A separately linked Lightrec fixture using the LTO/division libraries matches
the baseline in all 64 cases: PC, cycles, register/RAM hashes and exit flags.
Its execution total is 502 us versus the original loop baseline's 6,686 us;
this includes countdown folding and is not an isolated LTO speedup.

The current core-LTO option additionally excludes the GPU renderer and uses
`-fno-strict-aliasing` for the CPU/audio/JIT libraries' existing typed register
views. Run 22 archives that narrower variant without unrolled division.
These optional settings remain off by default until game comparisons finish.

Run 21 sequences 810–996 averaged **11.4646 presentations/s** over 98.739
emulated seconds (minimum 9.3023, guest vblanks 26.2105/s, 12.559% skip
decisions, no interval at 19). Thus broad core/GPU LTO plus unrolled division
with HLE has not improved on the earlier 11.7533/s HLE sample. Its rendering
was intact through the measured ranch camera movement. Keep the OpenBIOS
run 19 result as the best sustained result. Run 23 combines OpenBIOS with
the narrower LTO variant and unrolled division at 480p.

The final LTO division library also passes all 246,528 native checks when
the generated-code buffer is restricted to Lightning's declared size estimate;
the actual emitted size fits. Results are `div-lto-bounded.csv`.

Run 23 reached visible title and battle-demo scenes, but no ranch sample was
collected. Short Start commands repeatedly missed the title/menu transition.
The OpenBIOS diagnostic build (run 24, no LTO, unrolled division) subsequently
reached the main menu with a longer Start hold and loaded the existing ranch
save. Thus the title behavior alone does not establish an LTO regression.

Run 24 was interrupted by a benchmark-harness deadlock when requesting a Lua
checkpoint. Flycast's Lua save/load bindings synchronously stop the emulation
thread while the overlay holds the Lua mutex; the emulation thread may be
waiting for that mutex to deliver the harness's vblank callback. The harness
now rejects Lua state commands with an explicit status message. Use Flycast's
native state menu for checkpoints. Controller regression checks pass, including
rejection from both overlay and vblank paths. Run 25 repeats the diagnostic
with fresh logs and the corrected harness. The in-game VMU save is unaffected.

The private GTE translation reassociation experiment passed all 90,112 full
register comparisons on host and SH-4. Native RTPS improved roughly 3%, but
the dominant RTPT kernel was unchanged or slightly slower (125,836 to 126,332
us for the full-range workload). It remains outside production code.

Run 25 completed a 65.371-second diagnostic ranch sample (106–130): exclusive
PS1 execution 37.17%, register I/O 26.02%, geometry 19.99%, sound 10.46%, video
3.66%, draw 2.17%, interpreter 0.06%, other 0.46%. Diagnostic presentations
were 11.6565/s; this instrumented build is not a production FPS comparison.
The dispatch table still identifies the serial/timer polling area as expensive.

A second exact geometry experiment split the three signed products at the
12-bit binary point to avoid intermediate 64-bit sums. All 90,112 full-register
host and SH-4 comparisons passed, but RTPT regressed: 125,866 to 133,406 us
(full-range), 124,482 to 133,046 us (typical). It was rejected and remains a
private benchmark only. Run 26 instead tests OpenBIOS, 480p, O2 compilation,
unrolled division and one light skip in sixteen, with no LTO.

Run 26 sequences 1096–1282 averaged **13.9278 presentations/s** over 97.359
emulated seconds, minimum 11.0, guest vblanks 29.7045/s, 6.211% skip decisions
and no interval at 19. This is the best measured ranch average so far, with
AICA enabled. It improves useful presentation throughput rather than guest
vblank throughput; O2's independent effect has not been isolated.

The optional `OPT_DIRECT_HW_READ` candidate (default off) specializes already
tagged hardware byte/halfword/word loads. It retains the original handlers,
load-delay behavior, zero-register behavior and generic fallback. Validation
rejects overlapping or nonstandard hardware maps. All 50,000 randomized host
comparisons pass; native integration and game performance are pending.

All 40 host regressions pass with the candidate. Native SH-4 integration also
matches the baseline in all 64 cases (PC, cycles, full register/RAM hashes,
exit flags and hardware-read counts). The fixture uses byte, signed/unsigned
halfword and word loads across physical/cached/uncached hardware aliases and
a RAM fallback after the block was tagged. Hardware cases improved 37.83%
(3,829,879 to 2,381,008 us combined), while forced RAM fallback regressed 21.46%
(990,467 to 1,202,984 us). This tradeoff requires game measurement; run 27 uses
the candidate with the same settings as run 26.

Run 27 sequences 414–600 averaged **12.7337 presentations/s** over 98.322
emulated seconds, minimum 9.6246, guest vblanks 27.1760/s, 6.274% skip decisions
and no interval at 19. It does not improve the run 26 result. Camera/animation
phases are not identical, but this provides no game-level justification for
enabling the optional hardware-read path. Keep it off in the preferred build.

The optional `OPT_BOUNDED_IO_POLL` candidate recognizes the executed code of
a timer-2/serial-interrupt wait, without a game address. It folds complete
iterations only for the validated 1.75-cycle compiled path, CPU/8 timer mode,
canonical RAM, and a clean entry without pending loads or interrupts. Deadline,
timeout, wrapping and other paths remain ordinary execution. All 20,000 host
comparisons pass; the complete host suite now has 41 passing regressions.

Native integration through the actual Lightrec callback matches PC, cycles,
all register state, mode and exit flags in 256 cases. Combined fixture time
falls from 1,209,294 to 559,862 us, with 392,332 versus 110,443 MMIO reads.
These synthetic results do not establish a game speedup.

Run 28 sequences 316–502 averaged **12.7586 presentations/s** over 97.895
emulated seconds, minimum 8.6705, guest vblanks 27.2333/s, 6.288% skip decisions
and no interval at 19. It uses the run 26 settings plus timer folding. This
first sample includes the initial barn view and the subsequent outdoor camera
movement; camera phases differ from run 26. The candidate has not demonstrated
an improvement. A diagnostic variant is being prepared to measure eligibility
and actual folds before changing the preferred build. Training menus and the
selected Domino activity launch successfully; no additional save was written.

Run 28 subsequently completed Domino training and displayed its stat increase.
Run 29's 60.330-second diagnostic sample (98–120) attributes 38.16% to PS1
execution, 26.75% to register I/O, 19.06% to geometry and 9.64% to sound.
Its timer counters show that outer-entry folding is rare. Most sampled tail
entries already have the serial interrupt asserted, and the long waits occur
inside compiled blocks. The outer-entry candidate remains off for further
performance comparisons. The original native fixture also routed I_STAT via
a callback; validation must cover PCSX's direct I_STAT loads before promotion.

An audit found that run 27's hardware-read guard required a 0x2000-byte map,
but PCSX provides 0x8000 bytes. Its fast path therefore stayed disabled. The
guard now accepts an aperture covering the supported window while retaining
ordered lookup outside that window and for overlapping maps. All 41 host
regressions pass, including the production aperture. Native integration with
the 0x8000-byte map matches all 64 original baseline states and read counts;
hardware fixture time is 2,381,009 us versus 3,829,879 us.

Run 30's first ranch sample (430–616) averages **13.3046 presentations/s** over
98.011 emulated seconds, minimum 11.2254, guest vblanks 28.3846/s, exactly 6.25%
skip decisions and no interval at 19. It tests the corrected hardware-read
guard with timer folding off. A longer camera sample is still being collected;
the 19 FPS goal remains unmet.

Run 30's second consecutive sample (618–804) averages **13.0615
presentations/s** over 97.768 emulated seconds, minimum 10.7738, guest vblanks
27.8619/s, 6.301% skip decisions and no interval at 19. The corrected read path
still does not establish an improvement, so it is off in the next candidate.

The SH-4 divider now uses a 256-byte leading-zero table for its nonzero 16-bit
denominator, avoiding the generic software CLZ call. Other architectures retain
the builtin. All 65,535 nonzero normalization inputs match, with 720,896 divider
edge comparisons and 90,112 complete GTE register comparisons in the private
host/native fixture. Native RTPT totals improve from 125,875 to 121,175 us
(full-range) and 124,490 to 118,092 us (typical). RTPS also improves. All **42
host regressions pass**. This remains a kernel result until game measurement.

The polling experiment is now being tested at the count read inside compiled
loops, rather than only at outer CPU entries. It remains optional and off by
default. Its compile-time predicate and runtime checks match the full executed
instruction pattern, synchronize the required input registers, and retain the
ordinary timeout/event iteration. Native integration for this revision is still
pending; the earlier outer-entry measurements do not validate the new hook.

The compiled-read revision now passes all **256 native integration cases**:
PC, cycles, full register hash, timer mode and exit flags match the baseline.
The fixture enters through the count read, the loop tail, and a prefix that
sets up constant hardware addresses. Folding occurs in 158 cases. Combined
fixture time is 680,474 versus 1,208,711 us; reads fall from 392,176 to 110,374.
Run 31 tests this revision plus the divider improvement, with direct hardware
read specialization off and the usual OpenBIOS/O2/480p/light-sixteen/AICA setup.

Run 31 (408–594) averages **13.3201 presentations/s** over 98.047 emulated
seconds, minimum 11.2570, guest vblanks 28.4149/s, 6.245% skip decisions and no
interval at 19. Run 32 confirms that the compiled hook is active: its ranch
sample (122–144, 60.386 seconds) folds 5,976 waits, skipping 5,574,976 equivalent
guest cycles with state preserved. Exclusive attribution is PS1 34.13%, I/O
28.73%, geometry 19.82%, sound 9.83%, video 4.36%, draw 2.63%.

Run 33 adds bounded, address-level I/O attribution using the existing timing
samples. It distinguishes read/write widths, groups SPU registers, and reports
dropped records. All 42 host regressions pass, including nested SPU attribution,
snapshot publication/reset and collision overflow. Performance comparisons
continue to use uninstrumented builds.

A private exact matrix experiment uses 32-bit dot products only when all nine
rotation coefficients fit signed 15-bit values, retaining the full path
otherwise. Exhaustive coefficient guards and 90,112 full-register comparisons
pass on host/native. RTPT improves only about 2% in the native fixture, so the
extra path has not been added to production.

Run 34's ranch sample (146-160, 40.394 seconds) isolates **26.13%** of total
runtime in linked-list GPU DMA. Separate bulk-upload DMA did not occur in this
sample; command lists can still contain VRAM transfer commands.
The full profiler adds substantial overhead; its 10.0757 presentations/s is
not compared with uninstrumented performance runs.

The strengthened native timer fixture found a mismatch when entry includes a
constant-address prefix (case 2: v1=249 baseline versus 248 candidate). Earlier
fixtures did not exercise this contiguous prefix. The optional fold is disabled
in subsequent game builds. A stricter block-entry-only hook is under validation;
the previous 256-case result is not sufficient to promote the broader hook.

An empty ordering-table fast path preserves each link, cycle count and loop
check while bypassing command-buffer and progressive-DMA handling. Mixed,
partial, mirrored, unaligned, circular and progressive lists match in 20,000
host/native cases. SH-4 fixture times: all-empty 142,392 to 61,811 us; one payload
per sixteen nodes 153,210 to 84,371; one per four 193,223 to 152,134. Progressive
DMA retains its original path (4,977 to 4,895 us). All 43 host regressions pass.
Run 35 tests the uninstrumented candidate with timer folding off, 200 MHz,
OpenBIOS, AICA and light-sixteen.

Run 35 (548-734) averages **14.0715 presentations/s** over 97.360 emulated
seconds, minimum 10.5973, 30.0329 guest vblanks/s, 6.2759% skip decisions and
zero intervals at 19 FPS. Camera phases differ from run 26; this modest increase
does not establish an isolated DMA gain. The target is still unmet.

The block-entry-only timer hook matches all 256 stronger contiguous-prefix
fixture cases, including the previously failing case 2. Folding occurs in 158
cases. It remains off in run 36 while GPU attribution is refined. That profiler
now charges renderer synchronization, queue drains and cache updates to drawing;
command lists can trigger this work inside register writes.

Run 36 (170-184, 40.448 seconds) attributes 24.35% to drawing, 38.07% to PS1
execution, 19.01% to GTE, 8.46% to sound and 5.84% to actual register/DMA work.
The earlier 26% GPU-register attribution included deferred drawing. Empty-list
walking is therefore a much smaller opportunity than that number suggested.

The hardware-only drawing-area shortcut passes one million clipping cases and
existing blanking/software readback checks (44 regressions total at that point).
Native visible-sprite setup costs 101,246 versus 186,465 us. Run 37 tests it
without UV arithmetic changes, timer folding or LTO. A subsequent UV arithmetic
candidate preserves one million randomized triangles and 1,944 reflected,
axis-aligned cases. Native general-triangle time falls 154,880 to 103,746 us;
mirrored-sprite time is 170,929 versus 168,444 us. Neither kernel result is an
established full-game improvement yet.

Run 37 (712-898) averages **13.5462 presentations/s** over 97.961 seconds,
minimum 10.2996, guest vblanks 28.9095/s, skips 6.2767%, no interval at 19.
It does not demonstrate a full-game gain. Run 38 combines the validated
visibility and UV changes with core-only LTO (renderer excluded), still O2,
200 MHz, OpenBIOS, AICA, light-sixteen and timer folding disabled.
