# Docker Dreamcast development environment

## Windows validation checkpoint (2026-09-20)

The current Windows host has a running `bloom-dc` container, with KOS and
kos-ports in the `bloom-sdk` volume at `/sdk`. It uses the same pinned KOS
and kos-ports revisions below, but its compiler reports **GCC 17.0.0
20260630 (experimental)**. Do not treat it as the GCC 15.1 environment
described in the rest of this document.

Validation of revision `d45ba6d`, with the shell line-ending fix:

- All 13 host regression checks passed in `bloom-tests` with sanitizers.
- Both PVR and Unai compiled and linked, and BIOS packaging completed.
  Each ELF contains a 512 KiB `.bios` section.
- Local FFVI images and matching ELFs are in `build/validation/`:
  `bloom-ff6-pvr.cdi`, `bloom-pvr.elf`, `bloom-ff6-unai.cdi`, and
  `bloom-unai.elf`. Both use `/cd/ff6.chd` and embedded OpenBIOS.
- The PVR image passed the Dreamcast license screen in Flycast 2.7. After
  an initially gray screen, a user-provided screenshot confirmed FFVI's
  "Published by Square Electronic Arts L.L.C." startup screen. Flycast's
  overlay showed 7.4 FPS; this does not establish native Dreamcast speed.
  No title screen, gameplay, or game audio was verified. The Unai image
  has not been runtime-tested.

The first build failed at BIOS packaging because Windows checkout conversion
gave `insert_bios.sh` a CRLF shebang. `.gitattributes` now requires LF for
shell scripts. Existing checkouts may need their scripts checked out again
after preserving any local edits.

The existing container mounts an older worktree as `/workspace`, so compiling
that mount does not validate the current checkout. This run built an isolated
source snapshot under `/tmp/bloom-current-source`, with build directories
`/tmp/bloom-current-pvr` and `/tmp/bloom-current-unai`, then copied artifacts
to the current checkout. For another revision, refresh the source snapshot
first. The base Docker image alone lacks CMake; these builds used the
additional tools already installed in `bloom-dc`.

The subsequent local performance builds are in `build/validation/`:
`bloom-ff6-perf.cdi` adds the solid-triangle fast path and timing logs;
`bloom-ff6-perf2.cdi` also adds incremental software lines;
`bloom-ff6-perf3.cdi` adds sparse/full texture-upload dispatch and `PERF-PVR`
workload counters. Matching debug executables are `bloom-perf.elf`,
`bloom-perf2.elf`, and `bloom-perf3.elf`. All use the FFVI image already
supplied on this host, embedded OpenBIOS, PVR, and `WITH_PERF_LOG=ON`.
Host regression and cross-build checks passed; these performance images have
not established a title-screen/gameplay or native-hardware speed improvement.

The subsequent audit passed all 17 sanitizer regression checks and rebuilt PVR
with performance logging enabled and Unai with it disabled in both Docker
(GCC 17) and native DreamSDK (GCC 15.1). Native build outputs and instructions
are described in [the DreamSDK guide](dreamsdk-windows.md). The existing CDI
files above are earlier checkpoints; they were not repackaged during the audit.
See [performance notes](performance.md) for the audited host benchmark results.

## Earlier GCC 15.1 environment

The local `bloom-dreamcast-sdk:gcc15.1` image and `bloom-gcc15` container
contain an ARM64 Linux toolchain. Start
Docker Desktop first. On macOS, if Docker is not on your PATH:

```sh
export PATH="/Applications/Docker.app/Contents/Resources/bin:$PATH"
```

Installed versions:

| Component | Version |
| --- | --- |
| SH-4 GCC | 15.1.0 |
| KallistiOS | `804b3195ebd1a06a27cc2b3a5eacf7a2429040a3` plus compatibility entry point below |
| kos-ports | `f4faacc42faaf552625777b7709e871a827e1055` |
| mkdcdisc | `4d74e40dd2122e14389a305ed1d86dd024201389` |

The GitHub `dreamcast` workflow uses the same KallistiOS and kos-ports
revisions as this table. Unpinned KOS HEAD is not compatible with that
kos-ports libjpeg (`uint16`).

The compiler archive is the Linux AArch64 asset from
Its verified SHA-256 is
`cd80e020cc4969b1fd3c1b36522b2725c83f08b634b15e6cb17ab48194604235`.
The archive's KOS and ports were replaced with the pinned revisions above;
the bundled KOS lacks graphics APIs required by Bloom. Tsunami, Parallax,
zlib, libpng, libjpeg, and libkmg were rebuilt against this SDK.

## Build and package the mounted game

The installed container mounts this checkout at `/workspace` (read-only),
`build/docker` at `/out`, and the supplied CHD at
`/game/streetfighteralpha3.chd` (read-only). Its toolchains and intermediate
builds live in the container. The reusable image preserves the installed
SDK independently of that container and does not contain the mounted CHD.
Image ID: `7ce19827478d70e75f5180b2c238b947b6d9f598c514ed56d5ad364bfd0fb4ab`.

If the container is later removed, recreate it from the repository root:

```sh
mkdir -p build/docker
docker run -d --name bloom-gcc15 \
  -v "$PWD:/workspace:ro" -v "$PWD/build/docker:/out" \
  -v /absolute/path/streetfighteralpha3.chd:/game/streetfighteralpha3.chd:ro \
  bloom-dreamcast-sdk:gcc15.1 sleep infinity
```

```sh
docker start bloom-gcc15
docker exec bloom-gcc15 bash -lc '
  source /opt/toolchains/dc/kos/environ.sh
  set -e
  kos-cmake -S /workspace -B /tmp/bloom-game \
    -DWITH_GAME_PATH=/cd/streetfighteralpha3.chd \
    -DWITH_EMBEDDED_BIOS_PATH= \
    -DCMAKE_C_FLAGS= -DCMAKE_CXX_FLAGS=
  cmake --build /tmp/bloom-game -j4
  sh-elf-objcopy -O binary /tmp/bloom-game/bloom.elf /tmp/bloom-game/bloom.bin
  cp /tmp/bloom-game/bloom.elf /out/bloom.elf
  rm -f /out/bloom-sfa3.cdi
  /opt/toolchains/dc/mkdcdisc/build/mkdcdisc \
    -b /tmp/bloom-game/bloom.bin -f /game/streetfighteralpha3.chd \
    -N -n "Bloom Audio Test" -o /out/bloom-sfa3.cdi
'
```

Open `build/docker/bloom-sfa3.cdi` in Flycast. Packaging uses the raw binary
because Bloom's embedded BIOS is inserted after linking. The `-N` option
omits mkdcdisc's large padding track. The original CHD is never modified.

Flycast 2.7's ARM64 fast CPU backend asserts in `bm_AddBlock` when this
build enables the MMU. For diagnosis, launch with the interpreter and
serial logging (replace the app path if necessary):

```sh
/path/to/Flycast.app/Contents/MacOS/Flycast \
  -config config:Dynarec.Enabled=no,config:Debug.SerialConsoleEnabled=yes \
  "$PWD/build/docker/bloom-sfa3.cdi"
```

These are temporary launch overrides. The interpreter run loads the CHD,
reads its two tracks, and identifies `STREET_FIGHTER_ALPHA3` / `SLUS00821`.
The initial embedded-OpenBIOS run remained black after initialization;
it has been rebuilt with the PVR fix below but not rechecked at runtime.

An additional diagnostic image is in `build/docker`:

- `bloom-sfa3-unai.cdi`: Unai with built-in BIOS emulation. Displays Street
  Fighter Alpha 3's loading screen under Flycast's interpreter. It remained
  there during the observed run; title-screen/gameplay progression and game
  audio are not verified. `bloom-unai.elf` is the matching debug executable.

The Unai variant was configured with `-DGPU_PLUGIN=Unai` and
`-DWITH_EMBEDDED_BIOS_PATH=`. The normal build still embeds OpenBIOS.

### PVR blanking fix

`bloom-pvr-verified.cdi` and `bloom-pvr.elf` contain the updated PVR renderer
with built-in BIOS emulation. The original overflow trace had GPU status
`5481260a` (display disabled): gpulib skips presentation while blanked, but
Bloom was still opening PVR scenes and adding clip records. Blanked draws
now update VRAM in software, and the display-blank callback closes the old
scene and presents black. The game progressed through the QSound screen
and animated intro in the runtime check, without the clipping-error flood.
Thin vertical seams remain visible. Gameplay and game audio are not yet
verified.

The source-based PVR regression test also exposed undefined signed shifts in
coordinate decoding. Coordinates now use explicit 11-bit sign extension;
the host sanitizer suites pass with nonrecovering address/undefined sanitizers.

### Texture-cache update boundaries

`bloom-pvr-cache.cdi` and `bloom-pvr-cache.elf` additionally fix texture-cache
invalidation at the right and bottom of VRAM updates. The previous calculation
passed inclusive endpoints to a function expecting exclusive endpoints, so
single-pixel updates and updates ending just inside a new cache block could
leave stale data. Tests cover all 524,288 single-pixel locations and a range of
rectangles crossing block boundaries.

The cache build was rechecked in Flycast: it reaches the QSound screen and
animated character intro without a clip-area overflow. The fixed-position
vertical streaks remain, so this cache correction does not establish their
cause. Title-screen/gameplay and in-game audio remain unverified.

To rebuild this test configuration, use the commands above with
`-B /tmp/bloom-pvr -DGPU_PLUGIN=PVR -DWITH_EMBEDDED_BIOS_PATH=` and package
that build's `bloom.elf` after converting it to a raw binary.

## Compatibility with the prebuilt compiler

This compiler's libgcc calls an external `mutex_lock`. Current KOS makes
that function inline, so an unmodified combination resolves the call to
libgcc's weak bootstrap stub, which returns failure. C++ startup then
asserts when it tries to unlock the unacquired mutex.

The installed SDK appends this compatibility entry point to
`kernel/thread/mutex.c`, then rebuilds KOS:

```c
int kos_legacy_mutex_lock(mutex_t *m) __asm__("_mutex_lock");
int kos_legacy_mutex_lock(mutex_t *m) {
    return mutex_lock_timed(m, 0);
}
```

This restores locking rather than disabling assertions or skipping C++
initialization. A compiler rebuilt against current KOS's `gthr-kos.h`
calls `mutex_lock_timed` directly and does not need this entry point.
After changing KOS, remove the generated `bloom.elf` before rebuilding;
CMake does not track every library injected by the KOS compiler wrapper.

## Verification scope

`bloom-tests` runs the host regression checks separately from this SDK.
Those checks use sanitizers and hardware stubs. The standalone Flycast
audio smoke test passed both playback rounds with listener confirmation.
Neither establishes PlayStation game compatibility or physical Dreamcast
behavior; those require separate runtime checks.
