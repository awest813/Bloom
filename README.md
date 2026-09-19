Bloom
=====

Bloom is an experimental PlayStation 1 emulator for the SEGA Dreamcast.

It is built on top of other open-source projects:

- KallistiOS, the independent SDK for the SEGA Dreamcast
  (https://github.com/KallistiOS/KallistiOS)

- PCSX, the PlayStation emulator
  (https://github.com/libretro/pcsx_rearmed)

- Lightrec, MIPS-to-everything JIT compiler
  (https://github.com/pcercuei/lightrec)

- GNU Lightning, an arch-independent run-time assembler
  (https://www.gnu.org/software/lightning)

- OpenBIOS, a PlayStation 1 BIOS re-implementation
  (https://pcsx-redux.consoledev.net/openbios/)

This is still alpha software: 2D titles can be playable, 3D titles are often
too slow, and several core features are incomplete. See `ROADMAP.md` for the
plan to close those gaps.

Features
--------

- Hardware CD-ROM support, including original discs, even the
  copy-protected (libcrypt) ones

- BIN/CUE, CCD/IMG, MDS/MDF, ISO, PBP, and CHD images with FLAC/LZMA/ZSTD
  compression are supported

- Can load image files from CD, IDE (hard drive) or SD cards

- Using OpenBIOS for the BIOS; official BIOS dumps can optionally be used

- Experimental PVR renderer (faster, lower compatibility)

- Optional software renderer (Unai; slower, much higher compatibility)

- Memory cards emulated as files on the VMUs; memory card images on IDE or
  SD cards are also supported

- Analog controllers, mouse, rumble, and button combos for missing PS1
  buttons (see Controls)

- Optional savestate load at boot (`WITH_BOOT_SSTATE`)

Known limitations
-----------------

- **No game audio yet.** The default AICA SPU plugin only implements register
  and DMA access so games do not hang on SPU probes. There is no ADPCM, XA,
  CDDA, ADSR, or reverb output. `SPU_PLUGIN=Null` uses pcsx_rearmed dfsound
  with a silent backend and does emulate SPU IRQs (needed by some games such
  as Metal Gear Solid).

- The PVR renderer does not implement texture windows, off-screen VRAM
  rendering, or several other GPU features. Use Unai when a title needs
  accurate drawing.

- The in-menu **Options** screen is currently read-only. Renderer, resolution,
  and similar flags are compile-time CMake options.

- There is no in-game pause menu, disc swap UI, or savestate UI.

Controls
--------

Dreamcast controller mapped as a DualShock-style pad:

| Dreamcast              | PlayStation                          |
| ---------------------- | ------------------------------------ |
| A                      | Cross                                |
| B                      | Circle                               |
| X                      | Square                               |
| Y                      | Triangle                             |
| L / R triggers         | L1 / R1                              |
| C / D                  | L2 / R2                              |
| Z                      | Select                               |
| START                  | Start                                |
| START + A              | Select                               |
| START + X / B          | L3 / R3                              |
| START + L / R          | L2 / R2                              |
| START + analog stick   | Right analog stick                   |
| START + A+B+X+Y        | Quit emulator                        |
| START + D-pad Up       | Screenshot to `/pc` (dc-load only)   |

Hold START with another button to send the combo instead of Start.

Building
--------

You need the latest version of KallistiOS `master` branch installed, and
preferably a dc-chain toolchain built with the `gcc-15.0.0-lra` profile.
If you upload builds with dc-tool, you also need the latest version of both
dc-tool and dc-load.

Required kos-ports include Parallax and Tsunami (for the menu).

To build Bloom, run:

```
cd /path/to/bloom
mkdir build
cd build
kos-cmake ..
make
```

This will build Bloom with the default settings.

To configure Bloom you can use `kos-ccmake` instead, which will open a
(curses-based) user interface with all the options for the project.

Useful CMake options:

- `GPU_PLUGIN` — `PVR` (default) or `Unai`
- `SPU_PLUGIN` — `AICA` (default, silent) or `Null` (silent, SPU IRQs)
- `WITH_480P`, `WITH_HYBRID_RENDERING`, `WITH_CHD`, `WITH_IDE`, `WITH_SDCARD`

Building a 1ST_READ.BIN
-----------------------

To build a bootable 1ST_READ.BIN binary for burning to a disc, first build
Bloom normally, then run:

```
kos-objcopy -O binary bloom.elf bloom.bin
${KOS_BASE}/utils/scramble/scramble bloom.bin 1ST_READ.BIN
```

This 1ST_READ.BIN file can then be burned using e.g. BootDreams, or a CDI can
be created using e.g. mkdcdisc.

Note that using the bloom.elf file with mkdcdisc directly will not work.

Building with debug support
---------------------------

Bloom can be built with full debug output, including the log of the
optimizer, the PSX code disassembly, and the SH4 code disassembly.

It is however necessary to first build and install Binutils into the KOS
toolchain. To make it easier, there is a script that will automatically
download, build and install Binutils:

```
cd /path/to/bloom
deps/binutils/build.sh
```

Then, to build Bloom with debug support:

```
cd /path/to/bloom
mkdir build
cd build
kos-cmake -DCMAKE_BUILD_TYPE=Debug -DLOG_LEVEL=Debug ..
make
```
