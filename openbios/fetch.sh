#!/bin/sh
# Refresh openbios.bin from the Libreboot 20241206 PlayStation ROM set
# (MIT-licensed PCSX-Redux OpenBIOS).
set -eu
DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
EXPECTED="$DIR/openbios.bin.sha512"
URLS="
https://mirror.koddos.net/libreboot/stable/20241206/roms/playstation/openbios.bin
https://www.mirrorservice.org/sites/libreboot.org/release/stable/20241206/roms/playstation/openbios.bin
"

tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT

ok=0
for url in $URLS; do
	if curl -fsSL -o "$tmp" "$url"; then
		ok=1
		break
	fi
done
if [ "$ok" -ne 1 ]; then
	echo "fetch.sh: could not download openbios.bin" >&2
	exit 1
fi

(
	cd "$DIR"
	# sha512sum -c expects the file named in the checksum file.
	cp "$tmp" openbios.bin
	sha512sum -c "$EXPECTED"
)
