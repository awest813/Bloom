# Constant GPU-status polling shortcut

September 24, 2026. This follows the earlier single-sync polling candidate.
During a detected polling burst, no guest command or event runs between
samples. If the synchronized status already has the non-busy bit set, and
the line bit is either set or disabled by display timing, the returned status
cannot change as the cycle counter advances. The new helper can jump to the
first original sample at or beyond the next interrupt.

The shortcut runs only when the first read still equals the previous status.
It retains the original stride and interrupt overshoot, skips no first read
or plugin synchronization, and falls back near unsigned wraparound. Bursts
of eight samples or fewer retain the loop to avoid division overhead. All
nonconstant statuses retain the existing loop.

The differential regression compares 30,000 reads against the original
`psxHwRead32` loop, including stale plugin status, changing timing bits, busy
equality, varying strides, non-GPU reads, and cycle wraparound. It now asserts
that over 100 bursts actually take the new path. Explicit tests cover exact
interrupt hits, overshoot, disabled line-bit timing, zero stride and overflow.

## SH-4 kernel experiment

`tests/benchmark_gpu_poll.c`, built with DreamSDK GCC 15.1 at `-O3`, runs in
200 MHz Flycast. Each result sums two alternating-order runs of 10,000 bursts
after warmup. Bursts begin at cycle 100, with the next interrupt at 2200.
These are kernel times, not game FPS or physical Dreamcast results.

| Status | Stride | Original us | Candidate us |
| --- | ---: | ---: | ---: |
| Constant | 1 | 2324190 | 9129 |
| Constant | 4 | 584822 | 9440 |
| Constant | 16 | 148995 | 9404 |
| Constant | 64 | 39790 | 9433 |
| Constant | 256 | 13310 | 11341 |
| Changing line bit | 1 | 2156461 | 2158265 |
| Changing line bit | 4 | 545338 | 542165 |
| Changing line bit | 16 | 137836 | 139672 |
| Changing line bit | 64 | 37476 | 39326 |
| Changing line bit | 256 | 12100 | 13946 |

Long constant bursts improve substantially. The guard has a cost: short,
changing bursts are slower, by about 15% in the stride-256 case. Whether the
tradeoff helps FFVII requires gameplay measurement; no game-level gain is
claimed. The native benchmark confirmed matching ending cycles and completed.

ELF: `build/validation/gpu-poll-skip.elf`, SHA-256
`48a7c162aa15b72a54ccd909bef32c90075dcac0954d62df6b08e228dc772832`.
It ran in the existing standalone `flycast-sprite-bench` directory. The
separate `dreamsdk-hw-skip` build combines this candidate with the targeted
raw 16-bit sprite path. The running `flycast-hw-sprite` game contains only
the sprite change, so its measurement remains a distinct comparison.
