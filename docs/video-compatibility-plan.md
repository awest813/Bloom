# Video compatibility audit and implementation plan

Audit date: 2026-09-20. Scope: current working-tree PVR renderer, software
fallback, presentation code, gpulib integration, bundled Unai, and host tests.
The findings below describe the starting point. Implementation progress is
tracked separately so historical defects are not confused with remaining work.

## Implementation progress

The first compatibility batch implements safe presentation copying and the
following draw-state fixes:

- `copy_scanout()` accepts byte offsets without alignment assertions, converts
  only requested pixels, and initializes store-queue padding to black. Complete
  aligned blocks retain word-at-a-time conversion. Reads beyond the 1 MiB VRAM
  allocation become black rather than accessing unrelated memory. The existing
  linear source offset and 2048-byte row stride are preserved; hardware-specific
  edge-wrapping behavior still needs stage 5 validation.
- Hardware polygons, lines, and rectangles now share signed 11-bit coordinate
  decoding with software. Empty/reversed drawing bounds stay empty after restore,
  and primitive dispatch skips empty drawing areas after applying state updates.
- Textured polygons update the retained texture-page state; software texture
  blending uses the primitive's own mode. Dither/display-control bits survive
  texture-page updates.
- E2 windows use the exact bitwise mask in software and window-origin mapping.
  Hardware sprite/polygon splitting uses the first masked-bit boundary, which
  supports non-contiguous masks without reducing them to a repeating small tile.

Added display-copy sanitizer coverage, exhaustive one-axis texture-window and
coordinate checks, sprite coverage checks, and blend/restore-state regressions.
These are routine-level checks, not completed game-scene validation.

The next batch starts stage 3 with an opt-in comparison mode:

- `GPU_PLUGIN=PVR` plus `WITH_PVR_SOFTWARE=ON` routes visible and offscreen
  primitives through the existing software rasterizer from startup, then presents
  PS1 VRAM using the bounded display-copy path. Hardware-only menu settings are
  disabled. This is a diagnostic baseline, not a performance optimization or a
  claim of complete PS1 raster accuracy.
- `renderer_sync()` now drains queued commands before CPU readback, VRAM copies,
  and save-state snapshots. State restoration drains old draws before applying
  the restored settings. Normal hardware rendering still does not copy its pixels
  back into PS1 VRAM; draining commands alone cannot repair that gap.
- Software presentation submits an empty black scene when scanout is disabled.
- Added production-rasterizer tests for visible writes, synchronization, texture
  reuse, CPU-copy reuse, and mask protection, plus a state-restore ordering check.
  The queue and copy adapters in these host tests are simulated; they do not
  replace a complete gpulib/game integration test.

For a separate DreamSDK comparison build, use:

```sh
scripts/build-dreamsdk.sh build/dreamsdk-pvr-software \
  -DGPU_PLUGIN=PVR -DWITH_PVR_SOFTWARE=ON -DWITH_PERF_LOG=ON
```

The following rasterizer batch fixes shared-edge ownership before extending
software replay. Flat, shaded, and textured triangles now include top/left edges
and exclude bottom/right edges after winding normalization. Coverage bias is
kept out of the interpolation calculation. This prevents both triangles of a
quad from blending the same diagonal pixel twice in the software rasterizer.
It applies to the normal PVR backend's software draws and the diagnostic mode;
it does not establish that the reported hardware-rendered game seams are fixed.

The triangle fixture now uses an independent rational scanline coverage oracle.
It checks 240 generated triangles and 576 shared-edge combinations spanning both
diagonals, all vertex permutations, clipped/unclipped drawing, all four blend
modes, and flat/shaded/raw-textured paths. A negative control that removes the
coverage bias fails the new seam check as expected. All 21 sanitizer checks and
both DreamSDK PVR builds pass; logs are `build/validation/video-edges-*.log`.

The follow-up audit fixes presentation texture lifetime: wait for TA readiness
and render completion before overwriting or freeing the shared scanout texture.
KOS readiness alone can return while the rasterizer is still sampling it.
Closing a hardware PVR session in 24-bit mode no longer tries to finish a
hardware scene that was already closed when entering software presentation.
Lifecycle tests cover repeated presentation, blanking, 15/24-bit transitions,
closing in either mode, and reopening, with separate simulated TA/render states.
These checks verify call ordering; hardware timing still needs game validation.

Still open in stage 2: broader raster accuracy, including subpixel/rounding
agreement with PS1 hardware, and a
coherent per-primitive fallback when polygon splitting cannot represent a
primitive. Stage 3 still needs a production hardware/coherence strategy and
end-to-end readback/save-state validation. Stages 4–5 remain pending. No new
gameplay capture establishes that the reported seams are fixed.

Validation of this batch: all 21 host checks pass with address/undefined-behavior
sanitizers. DreamSDK builds succeed for hardware PVR, diagnostic PVR, and Unai.
Both PVR builds retain a 512 KiB embedded BIOS and stay within the two 8 KiB
code-section limits. Logs are in `build/validation/video-stage3-*.log`; the
diagnostic boot binary is `build/dreamsdk-pvr-software/1ST_READ.BIN`.

## Findings

| Priority | Finding and evidence | Likely effect / confidence |
| --- | --- | --- |
| P0 | `src/platform.c`: `dc_vout_flip()` asserts four-byte alignment; `copy15()` and `copy24()` read through `uint32_t *`. gpulib's `vout_update()` can supply a two-byte-aligned origin or a byte offset after 24-bit cropping. Width rounding also reads beyond the requested rectangle. | Confirmed unaligned read in an isolated UBSan probe of production `copy24()`. Possible aborts or corrupt video at shifted origins. Right/bottom edge overreads need boundary probes. |
| P0 | `src/pvr.c`: visible primitives normally bypass software VRAM writes; `renderer_sync()` is empty, and `hw_render_stop()` invalidates texture caches without copying rendered pixels into `gpu.vram`. `pvr_render_fb()` reuses the Dreamcast framebuffer, not PS1 backing memory. | Confirmed missing synchronization path. CPU readback, framebuffer copies, render-to-texture, and partly visible primitives can encounter stale VRAM. Specific affected game scenes remain unverified. |
| P1 | `texwin_set()` reduces masks to a power-of-two lookup. Mask=1, offset=0, U=16 selects 0 instead of 16 under the documented bitwise mapping. Bundled Unai uses the same table. | Confirmed semantic mismatch: 145,920 of 262,144 one-axis mask/offset/U combinations differ. This measures input combinations, not affected games. |
| P1 | Hardware polygon/sprite dispatch decodes coordinates as signed 16-bit values; software dispatch uses `psx_coord()` to decode 11 bits. `sw_sync_ecmds()` turns reversed drawing bounds into a larger area, while direct E3/E4 updates do not. | Confirmed inconsistencies. Results can change with visibility or state restoration. Reproducers should determine exact expected pixels before editing. |
| P1 | `sw_draw()` reads a textured polygon's texture-page attribute locally, but `sw_plot()` chooses blending from `pvr.gp1`. Hardware dispatch uses that polygon's local blend mode. Packet handling also needs an explicit audit of texture-page state persistence into subsequent sprites/lines. | Confirmed blend-source disagreement when the modes differ. State persistence is a follow-up risk, not yet a reproduced defect. |
| P1 | `process_poly_texwin_wrap()` returns false when its split budget expires; `process_poly_inner()` can then continue without applying the window if the range still does not fit. | Confirmed path without a correctness fallback. Small windows and large UV spans need a reproducer and bounded fallback. |
| P2 | Software triangles accept all three nonnegative edge tests, including shared edges. Dithering, E1 rectangle-flip bits, display-area draw control, and interlace behavior lack focused coverage. E1 stores only its low 11 bits. | Shared-edge double blending is a concrete risk; other omissions need command-level tests and hardware/spec confirmation. Do not equate Dreamcast output dithering with PS1 rasterization dithering. |
| P2 | The roadmap reports fixed vertical seams and hybrid-sensitive scenes. Current `poly_enqueue()` already closes PT, flushes buffered TR, and draws later primitives directly once TR is open. | Visual issues remain unverified in this audit. A second software-buffer overflow is not established by the current code; stress the actual list transitions before changing them. |

The [PSX GPU reference](https://psx-spx.consoledev.net/graphicsprocessingunitgpu/)
defines texture-window coordinates by bit masking, signed 11-bit drawing
coordinates, and separate draw-mode controls for dithering and rectangle flips.
Use these semantics and small hardware test cases where Unai shares an
approximation. Unai is a useful comparison backend, not an infallible oracle.

Two extracted-production probes and their output are retained locally under
`build/validation/video-audit/`. The earlier audit passed 17 sanitizer checks
and built PVR/Unai with DreamSDK and Docker. Those successes do not cover the
new findings or establish PS1 GPU accuracy. No new emulator interaction or
gameplay validation was performed for this audit.

## Implementation order

### 1. Establish repeatable comparisons and fix presentation bounds

Capture initial VRAM, E1–E6 state, display registers, and ordered GPU commands
for small synthetic scenes. Run identical inputs through separate PVR and Unai
builds; store raw VRAM checksums and pixel differences as well as screenshots.
Begin with nearest filtering, FSAA off, and a fixed output mode. Keep original
packets available before PVR transforms or splits them.

Fix source loading in `copy15()` / `copy24()` to handle the actual byte offset.
Separate logical width from store-queue padding: convert valid source pixels,
then initialize padded destination pixels without reading extra source bytes.
Decide row-edge wrapping/clipping from GPU display semantics explicitly.

Acceptance: ASan/UBSan tests pass for offsets 0–3, narrow and non-aligned widths,
last-row/right-edge rectangles, and alternating 15/24-bit scanout. Test mode
changes and blank/unblank presentation on Flycast afterward. This is the first
implementation change: small scope, confirmed defect, shared by both backends.

### 2. Unify primitive state and software correctness

Centralize coordinate and draw-state decoding before selecting a rendering
path. Preserve an empty drawing area on restore. Pass the effective blend mode
to software shading explicitly and verify texture-page changes between commands.
Audit primitive size rejection consistently across both paths.

Implement exact E2 mapping in software while keeping the fast path for masks
it can represent. Unsupported masks or exhausted splitting must request a
correctness fallback, not silently render unwindowed textures. Include an
explicit outcome for completely clipped/consumed/fallback primitives.

Acceptance: exhaustive one-axis E2 tests; 4/8/16-bit texture and palette cases;
coordinates differing only in unused high bits; reversed clipping after restore;
all four blend modes; mask-bit combinations; and a textured polygon followed
by a sprite using the updated state. Adjacent translucent triangles must not
double-paint their common edge. Existing optimization reference tests remain.

### 3. Make PS1 VRAM authoritative at observation points

This is the largest compatibility change. First add a diagnostic software
rendering mode from reset, using the corrected rasterizer and presenting its
VRAM. It gives a coherent baseline without switching halfway through a scene
whose earlier PVR-only pixels are missing from RAM.

Then prototype bounded replay of original commands into software VRAM before
CPU readback, VRAM copies, and texture/palette reads that depend on rendered
regions. Preserve command order and the state of source textures at each draw;
flushing before source VRAM mutations is essential. Track dirty regions and
measure replay cost before adding selective fast paths.

Do not simply draw one failed primitive in software over stale VRAM. Its blend
destination, mask bits, and earlier hardware draws must already be coherent,
and its result must be presented in order. PVR framebuffer readback is an
alternative to evaluate, but output scaling/color conversion and PS1 mask-bit
preservation prevent assuming it is a faithful copy.

Acceptance: draw→CPU read, draw→VRAM copy→texture sample, draw→palette reuse,
partly visible primitives, masked translucent draws, and save/load all agree
with an independent expected result. Buffer limits must trigger a bounded,
ordered flush or software mode, with no primitive loss.

### 4. Address visual fidelity and hybrid stress cases

Test tiny polygon buffers, PT→TR transitions followed by more opaque polygons,
mask passes, and multiple drawing-area changes. Count fallback reasons, split
budget exhaustion, maximum queued polygons, and replayed regions; workload
counts alone cannot reveal a wrong pixel.

Reproduce the reported SFA3 seams using numbered checkerboard textures and
pixel-difference images. Sweep 4/8/16-bit page crossings, palette edits,
mirroring, nearest/bilinear filtering, and FSAA. Change UV rounding or clipping
only after identifying which stage introduces the seam. Then validate PS1
dithering, rectangle flips, and display-area write restrictions separately.

Acceptance: the minimized seam case and hybrid stress scenes pass in Flycast;
confirm PVR-specific list, clipping, and mask behavior on Dreamcast hardware.

### 5. Validate display modes and games

Exercise 256/320/384/512/640 widths, NTSC/PAL heights, interlaced field changes,
display-origin changes, video playback, and 15/24-bit transitions. Distinguish
PS1 source display modes from the Dreamcast's 480p/FSAA output settings and the
compile-time `WITH_24BPP` output option.

Use the already supplied FFVI image for publisher→title→menu→gameplay transitions;
use SFA3's reported intro/seam scene when its image is available. Add a supplied
3D title that exercises framebuffer feedback or mask effects. Record BIOS,
game revision, exact scene/save/input sequence, backend, settings, screenshot,
VRAM result, and timing. Treat MGS hybrid behavior as an unconfirmed report
until a reproducible scene is available.

Release gate: new regressions pass with sanitizers; PVR and Unai build with
logging on/off; SH-4 `.subN` sections remain within their 8 KiB limits; RAM stays
within the Dreamcast budget; affected scenes improve without regressing the
baseline scenes. Report compatibility and frame-time cost separately. Do not
trade away restored pixels for an unmeasured speed claim.

## Scope boundaries

Keep GPU selection at build time for now. Avoid a renderer rewrite, speculative
per-game hacks, new visual enhancements, and unrelated audio/JIT changes.
Ship the five stages as small, independently reviewable fixes and tests.
Stages 1–2 can begin immediately; stage 3 needs an architecture prototype and
memory/performance measurements before committing to replay versus readback.
