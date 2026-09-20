# Dreamcast build environment

Bloom builds inside the same container image the `dreamcast` GitHub workflow
uses, so a local build and CI compile the same sources with the same compiler.
Everything below was re-run from scratch and verified on 2026-09-20 on x86-64
(Docker 29.6 on Windows 11); the commands are plain Linux container commands
and work the same from a macOS or Linux host.

## Pinned components

| Component | Version |
| --- | --- |
| Toolchain image | `pcercuei/dreamcast-toolchain@sha256:0de7d87311717225021374f1afc0f69c6efa4df64f9e31efc415083b3e458d15` (Alpine 3.24.1, linux/amd64) |
| SH-4 GCC | 17.0.0 20260630 (experimental) |
| SH-4 binutils | 2.45.1 |
| KallistiOS | `804b3195ebd1a06a27cc2b3a5eacf7a2429040a3` |
| kos-ports | `pcercuei/kos-ports` `f4faacc42faaf552625777b7709e871a827e1055` |
| mkdcdisc | `4d74e40dd2122e14389a305ed1d86dd024201389` |

The workflow pins the image **by digest**, not by the `15.0.0-lra` tag. That
tag is mutable and has already been rebuilt from GCC 15 to GCC 17, which
silently changes which workarounds apply: `CMakeLists.txt` only applies
`-ffp-contract=off` to `src/background.cpp` for GCC < 16. Unpinned KOS HEAD is
a separate hazard — it dropped `uint16`, which this kos-ports libjpeg still
uses.

## Create the container

`/workspace` is the checkout (read-only), `/sdk` a named volume holding the
built KOS so it survives container restarts, and `/out` wherever you want the
artifacts.

    docker volume create bloom-sdk
    docker run -d --name bloom-dc \
      -v "$PWD:/workspace:ro" \
      -v bloom-sdk:/sdk \
      -v "$PWD/build/docker:/out" \
      pcercuei/dreamcast-toolchain@sha256:0de7d87311717225021374f1afc0f69c6efa4df64f9e31efc415083b3e458d15 \
      sleep infinity

## Build KallistiOS and the ports

Once per volume. `libtsunami` pulls in libparallax, libpng, libjpeg, libkmg
and zlib, so it is the only port you have to ask for.

    docker exec bloom-dc sh -c '
      set -e
      apk --update add --no-cache coreutils cmake git
      mkdir -p /opt/toolchains/dc/bin /sdk

      cd /sdk && mkdir -p kos && cd kos && git init -q .
      git remote add origin https://github.com/KallistiOS/KallistiOS.git
      git fetch -q --depth 1 origin 804b3195ebd1a06a27cc2b3a5eacf7a2429040a3
      git checkout -q FETCH_HEAD

      cd /sdk && mkdir -p kos-ports && cd kos-ports && git init -q .
      git remote add origin https://github.com/pcercuei/kos-ports.git
      git fetch -q --depth 1 origin f4faacc42faaf552625777b7709e871a827e1055
      git checkout -q FETCH_HEAD

      cp /sdk/kos/doc/environ.sh.sample /sdk/kos/environ.sh
      sed -i "s|KOS_BASE=.*$|KOS_BASE=/sdk/kos|" /sdk/kos/environ.sh
      sed -i "s/-O2/-O3 -fno-PIC -freorder-blocks-algorithm=simple -fipa-cp-clone -flto=auto -ffat-lto-objects -DNDEBUG/" /sdk/kos/environ.sh

      . /sdk/kos/environ.sh
      make -C "$KOS_BASE" -j"$(nproc)"
      make -C "$KOS_PORTS/libtsunami" install
    '

## Build Bloom

    docker exec bloom-dc sh -c '
      set -e
      . /sdk/kos/environ.sh
      kos-cmake -S /workspace -B /tmp/bloom -DCMAKE_BUILD_TYPE=Release
      cmake --build /tmp/bloom -j"$(nproc)"
      cp /tmp/bloom/bloom.elf /out/
    '

This is the default configuration: PVR, AICA, 480p, hybrid rendering, CHD,
IDE, SD, and the packed OpenBIOS. The build is warning-clean apart from one
pre-existing `-Wunused-but-set-variable` in vendored GNU Lightning
(`deps/lightning/lib/jit_sh.c`). `objcopy` reports `allocated section '.bios'
not in segment` when converting to a raw binary; that is expected, and the
BIOS image does land in `bloom.bin` at `__bss_start`, where `copy_bios()`
reads it.

The `embed-bios` step runs `openbios/insert_bios.sh` directly, so shell
scripts must have LF line endings — otherwise the kernel cannot resolve the
`#!/bin/sh` interpreter and the build fails with `No such file or directory`
(exit 127). `.gitattributes` pins `*.sh`, `*.py` and `Dockerfile` to
`eol=lf` so a Windows checkout with `core.autocrlf=true` cannot reintroduce
this.

## Package a disc image with a game

`mkdcdisc` is not in the image; build it once into the volume.

    docker exec bloom-dc sh -c '
      set -e
      apk add --no-cache meson ninja build-base pkgconf libisofs-dev
      cd /sdk && mkdir -p mkdcdisc && cd mkdcdisc && git init -q .
      git remote add origin https://gitlab.com/simulant/mkdcdisc.git
      git fetch -q --depth 1 origin 4d74e40dd2122e14389a305ed1d86dd024201389
      git checkout -q FETCH_HEAD
      meson setup build --buildtype=release && ninja -C build
    '

Then build Bloom with the on-disc path of the image and pack both. Write the
CDI inside the container and copy it out afterwards: writing a few hundred MB
straight onto a bind mount is far slower, and launching Flycast on a
partially written CDI makes it exit immediately with status 0.

    docker cp /path/to/game.chd bloom-dc:/tmp/game.chd
    docker exec bloom-dc sh -c '
      set -e
      . /sdk/kos/environ.sh
      kos-cmake -S /workspace -B /tmp/bloom-game -DCMAKE_BUILD_TYPE=Release \
        -DWITH_GAME_PATH=/cd/game.chd
      cmake --build /tmp/bloom-game -j"$(nproc)"
      sh-elf-objcopy -O binary /tmp/bloom-game/bloom.elf /tmp/bloom-game/bloom.bin
      /sdk/mkdcdisc/build/mkdcdisc -b /tmp/bloom-game/bloom.bin -f /tmp/game.chd \
        -N -n "Bloom" -o /tmp/bloom-game.cdi
    '
    docker cp bloom-dc:/tmp/bloom-game.cdi ./build/docker/

`-N` omits mkdcdisc's large padding track. Packaging uses the raw binary
because the embedded BIOS is inserted after linking; passing `bloom.elf` to
mkdcdisc will not work. The source CHD is never modified.

## Run it in Flycast

    flycast -config config:Debug.SerialConsoleEnabled=yes ./build/docker/bloom-game.cdi

Flycast's dynarec **does** run this build on x86-64, MMU and all: a
170-second Final Fantasy VI session under Flycast 2.7 on Windows showed no
`bm_AddBlock` assert and roughly ten times the frame rate of the interpreter
(5.7 vs 0.5 Dreamcast fps on the same FMV scene). The `bm_AddBlock` assert
recorded earlier was on Flycast's ARM64 fast backend; there, add
`config:Dynarec.Enabled=no` to fall back to the interpreter.

Serial console output was not captured on Windows in this setup, with or
without `log:Verbosity=0`, so on-target `printf` tracing still needs a
macOS/Linux Flycast or dc-load.

## Verification status

Verified in this environment:

- The default configuration builds and links, including the packed OpenBIOS.
- `python3 tests/test_regressions.py` passes 13/13, as does
  `docker build -f tests/Dockerfile -t bloom-tests . && docker run --rm -v "$PWD:/workspace:ro" bloom-tests`.
- Final Fantasy VI (Final Fantasy Anthology, PS1 CHD) boots from `/cd` under
  Flycast with the packed OpenBIOS: publisher screen, then the intro FMV.
- Video then freezes mid-FMV while audio keeps playing. See `ROADMAP.md`.

Not established here: physical Dreamcast behaviour, gameplay past the intro,
and audio fidelity.

## Earlier runtime findings

These were established on a machine-specific ARM64 SDK
(`bloom-dreamcast-sdk:gcc15.1`) that this document no longer describes. The
findings stand; the build instructions that produced them do not.

- **PVR blanking.** The original overflow trace had GPU status `5481260a`
  (display disabled): gpulib skips presentation while blanked, but Bloom was
  still opening PVR scenes and adding clip records. Blanked draws now update
  VRAM in software, and the display-blank callback closes the old scene and
  presents black. Street Fighter Alpha 3 then progressed through the QSound
  screen and animated intro without the clipping-error flood. Thin vertical
  seams remain.
- **Coordinate decoding.** The source-based PVR regression test exposed
  undefined signed shifts; coordinates now use explicit 11-bit sign extension.
- **Texture-cache update boundaries.** Invalidation at the right and bottom of
  VRAM updates passed inclusive endpoints to a function expecting exclusive
  ones, so single-pixel updates and updates ending just inside a new cache
  block could leave stale data. Tests cover all 524,288 single-pixel locations
  and rectangles crossing block boundaries. The fix did not remove the
  fixed-position vertical streaks.
- **Unai.** A `-DGPU_PLUGIN=Unai` build displayed SFA3's loading screen under
  the Flycast interpreter and stayed there during the observed run.

### Compatibility note for the prebuilt AArch64 compiler

Only relevant if you use that archive rather than the image above. Its libgcc
calls an external `mutex_lock`; current KOS makes that function inline, so the
call resolves to libgcc's weak bootstrap stub, which returns failure, and C++
startup asserts when it unlocks a mutex it never acquired. Appending this to
`kernel/thread/mutex.c` and rebuilding KOS restores locking:

```c
int kos_legacy_mutex_lock(mutex_t *m) __asm__("_mutex_lock");
int kos_legacy_mutex_lock(mutex_t *m) {
    return mutex_lock_timed(m, 0);
}
```

A compiler built against current KOS's `gthr-kos.h` calls `mutex_lock_timed`
directly and does not need it. After changing KOS, remove the generated
`bloom.elf` before rebuilding; CMake does not track every library injected by
the KOS compiler wrapper.
