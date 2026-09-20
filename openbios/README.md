OpenBIOS
========

OpenBIOS is a reimplementation of the PlayStation BIOS, under an MIT
license, from [PCSX-Redux](https://pcsx-redux.consoledev.net/openbios/).

`openbios.bin` in this directory is the 512 KiB image published with
Libreboot 20241206 (`roms/playstation/openbios.bin`). That file is OpenBIOS,
not a retail dump. Verify it with `openbios.bin.sha512`, or refresh it with
`openbios/fetch.sh`.

Bloom packs this image into `bloom.elf` after linking when
`WITH_EMBEDDED_BIOS_PATH` points here (the CMake default). Pass
`-DWITH_EMBEDDED_BIOS_PATH=` to skip packing, or a different path to use
another BIOS.

To rebuild OpenBIOS from source, see:
https://pcsx-redux.consoledev.net/openbios/
