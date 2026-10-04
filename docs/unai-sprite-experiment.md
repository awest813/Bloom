# Unai sprite loop experiment

September 24, 2026. Standalone SH-4 code compiled with DreamSDK GCC 15.1,
`-O3 -fomit-frame-pointer`, executed in Flycast at 200 MHz. This is an
emulated kernel microbenchmark, not game FPS or physical hardware validation.

`tests/benchmark_unai_sprite.py` extracts the production sprite function and
emits two copies. The initial candidate adds `#pragma GCC unroll 2` to the pixel loop.
That experiment did not change the production renderer. Each timed run draws 200 raw sprites
of 160 by 64 pixels. Both paths are warmed and measurement order alternates.

| Texture format | Original times, microseconds | Unrolled times, microseconds | Result |
| --- | --- | --- | --- |
| 4-bit palette | 202500, 205003, 202529, 202499 | 197634, 197635, 197604, 197635 | About 2.7% less time |
| 8-bit palette | 141593, 141629, 141593, 141599 | 158084, 158041, 158084, 160575 | About 12.1% more time |
| 16-bit direct | 201693, 204197, 201693, 201698 | 125723, 125729, 125691, 125688 | About 37.9% less time |

SH-4 symbol sizes confirm the compiler applied the transformation: the
4/8/16-bit functions grow from 170/154/146 to 264/238/204 bytes. The larger
code is a tradeoff, not an unconditional improvement.

The native run completed all pixel comparisons. These compare entire VRAM
buffers for 80 randomized cases per format, both with and without mask-bit
setting. Cases include transparent colors, odd widths, texture masks,
interlaced lines, and destination overlap with texture or palette data.
Host AddressSanitizer/UndefinedBehaviorSanitizer checks also passed, although
that host compiler ignored the unrolling annotation under sanitization; it
does not establish correctness or performance of SH-4 code by itself.

The ELF is `build/validation/unai-sprite-experiment.elf`, SHA-256
`94f5c88d76c647ff142cd1f554e5fec8c6e2d1aef219df231e59b6ff199fc895`.
The run is `build/validation/flycast-sprite-bench`. Results were read from
the benchmark's framebuffer output, including its final comparison-pass message.

Reject blanket sprite-loop unrolling: it regresses the 8-bit path. A targeted
16-bit raw-sprite implementation is a candidate, pending workload evidence,
integration tests, and matched game measurements. This experiment says nothing
about lighting/blending variants, polygons, or a whole-frame speedup.

## Targeted implementation follow-up

The working renderer now has an SH-4-only raw 16-bit span helper. It is selected
only without lighting, blending, or destination mask checks; mask-bit setting
is supported. Other formats retain their original loops. Reads and writes
stay in pixel order, including overlapping texture/destination memory.

`--production --matrix` tests this actual helper and its dispatch against the
original function with only that dispatch removed. Widths are 8, 32, and 160;
each sprite is 64 pixels high. Results below sum four alternating-order runs
of 200 sprites after warming both functions:

| Texture pattern | Width | Original us | Integrated candidate us |
| --- | ---: | ---: | ---: |
| Opaque | 8 | 44640 | 30508 |
| Opaque | 32 | 167834 | 107493 |
| Opaque | 160 | 827256 | 520652 |
| Alternating transparent | 8 | 41522 | 27441 |
| Alternating transparent | 32 | 155508 | 95163 |
| Alternating transparent | 160 | 768228 | 456505 |
| Fully transparent | 8 | 38474 | 25362 |
| Fully transparent | 32 | 143175 | 87006 |
| Fully transparent | 160 | 701636 | 417979 |

These kernel timings improve by approximately 32–41%. The native comparisons
passed. Integrated benchmark ELF SHA-256:
`19bc79e6b941319d75c3835b8c019b72697a5d9ce99f1bd29f29fe7fc458006a`.
The normal sanitizer suite now includes the production helper and dispatch,
with explicit widths 1/2/3/7/8/31/32/1023 plus randomized cases and full-buffer
comparisons. All 29 regressions passed. The host check omits the compiler
unrolling hint; the native benchmark separately checks emitted SH-4 code.

The full Release/Unai/HLE/AICA build `build/dreamsdk-hw-sprite` succeeded.
`build/validation/flycast-hw-sprite` launches a new input-isolated, ten-part
diagnostic FFVII capture at 200 MHz. Its manifest records exact hashes and
settings. No game-level gain is established yet. Retaining this candidate
depends on representative gameplay and image checks, not these kernel timings.

The subsequent FFVII station sample (252–262) was 9.88 FPS over 30.17 emulated
seconds, at the same visually verified Barret dialogue. Drawing time per
presentation was approximately 26.46 ms, essentially unchanged from the
earlier diagnostic's 26.58 ms. This does not demonstrate a material station
speedup. Results and hashes are saved in `flycast-hw-sprite`; performance
in other scenes remains unmeasured.
