#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Pack a BIOS image into bloom.elf at __bss_start.

set -eu

if [ "$#" -ne 2 ]; then
	echo "usage: insert_bios.sh <bios.bin> <bloom.elf>" >&2
	exit 1
fi

BIOS=$1
ELF=$2

if [ -z "${KOS_CC_PREFIX:-}" ]; then
	echo "insert_bios.sh: KOS_CC_PREFIX is not set" >&2
	exit 1
fi

if [ ! -f "$BIOS" ]; then
	echo "insert_bios.sh: BIOS file not found: $BIOS" >&2
	exit 1
fi

if [ ! -f "$ELF" ]; then
	echo "insert_bios.sh: ELF not found: $ELF" >&2
	exit 1
fi

BSS_ADDR=$("${KOS_CC_PREFIX}-readelf" -s "$ELF" | awk '/ __bss_start/{print $2; exit}')
if [ -z "$BSS_ADDR" ]; then
	echo "insert_bios.sh: __bss_start not found in $ELF" >&2
	exit 1
fi

"${KOS_CC_PREFIX}-objcopy" --add-section ".bios=$BIOS" "$ELF"
"${KOS_CC_PREFIX}-objcopy" --change-section-address ".bios=0x$BSS_ADDR" "$ELF"
"${KOS_CC_PREFIX}-objcopy" --set-section-flags .bios=alloc "$ELF"
