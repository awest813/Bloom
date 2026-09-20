OpenBIOS
========

OpenBIOS is a reimplementation of the PlayStation BIOS, under an MIT license.

A pre-built `openbios.bin` is not stored in this tree. If you place one at
`openbios/openbios.bin`, CMake packs it into `bloom.elf` after linking. To
skip packing, configure with `-DWITH_EMBEDDED_BIOS_PATH=`.

Build OpenBIOS from [PCSX-Redux](https://pcsx-redux.consoledev.net/openbios/)
(`make -C src/mips/openbios` in that project) or take `openbios.bin` from a
PCSX-Redux distribution.

The sources and compiling instructions are available here:
https://pcsx-redux.consoledev.net/openbios/
