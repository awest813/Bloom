# Developer reference guide assessment

Reviewed September 24, 2026 against the supplied **PlayStation Emulation on
Sega Dreamcast: Developer Reference & Source Guide**, local Bloom/KOS source,
the Renesas SH-4 software manual, and PSX-SPX. The guide is useful as a source
directory. Its suggested optimizations need measurement and correctness checks.

| Topic | Relevance to Bloom |
| --- | --- |
| Lightrec and GNU Lightning | Already running. Investigate emitted SH-4 code, register spills, dispatch and I/O costs; warm station interpreter fallback is negligible. |
| Store queues | Already used by `copy_scanout` in `src/platform.c`; earlier upload optimizations are documented in the performance record. |
| SH-4 matrix instructions | `src/pvr.c` already uses `ftrv` for host vertex/texture-coordinate transformation. This is distinct from guest GTE emulation and the working Unai path. |
| GTE acceleration | Worth investigating for battle, where geometry occupied 16.63% of an observed window. Preserve guest registers and FLAG behavior. |
| PowerVR and VRAM | Useful longer-term renderer guidance. Correct framebuffer reads, copies, texture feedback and ordering remain prerequisites. |
| Cache and register pressure | Actionable: inspect hot generated blocks and Unai loops, then measure targeted changes. A larger code buffer does not automatically improve cache behavior. |
| KOS, mkdcdisc, Flycast, OpenBIOS | Already present as tools or options. Current comparisons use HLE; changing BIOS mode would change the benchmark configuration. |

## Corrections and constraints

The 200 / 33.8688 ratio is about 5.9 host clocks per guest clock, not a universal
ceiling per PS1 instruction. Instruction timing, stalls and other emulated
hardware work also matter.

Four-cycle matrix arithmetic is not the cost of emulating a complete GTE
command. Loading, conversion, dependent-result latency and status handling
add work. Renesas documents approximate FTRV/FIPR arithmetic and distinct
result latencies. See sections 8 and 9.47 of the
[SH-4 software manual](https://www.renesas.com/en/document/mas/sh-4-software-manual).

GTE behavior includes fixed-point intermediate results, overflow flags,
saturation, shifts and perspective division. Floating-point matrix operations
are not automatically equivalent. The local `gte.c` implements these details;
see the [PSX-SPX GTE specification](https://psx-spx.consoledev.net/geometrytransformationenginegte/).
FSCA's sine/cosine calculation is not a general replacement for them.

Register reservation also has runtime constraints. Local KOS uses GBR for
thread-local storage; Lightrec's GBR-state option is off. Enabling it requires
an ABI/TLS preservation design.

## Updated priority

The isolated station sample was 10.18 FPS: execution excluding geometry
45.94%, drawing 26.95%, geometry 7.71%. A fixed-workload model eliminating all
measured geometry cost yields only about 11.03 FPS. This is a prioritization
estimate, not a forecast or hardware limit.

Keep execution and Unai drawing first. Use the guide's SH-4 references to
inspect register allocation, scheduling and memory behavior in those paths.
Treat an accurate GTE fast path as a separate battle-focused experiment.
Retain the 200 MHz, audio-enabled benchmark and verify images and guest
progress as well as FPS.

No emulator settings or implementation were changed solely on this guide's
authority, and no new FPS gain was measured during the review.
