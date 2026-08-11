#!/bin/sh
# Build VENTURE.PRG -- VENTURE (Exidy 1981 port) (cc65). --signed-chars per convention.
set -e
cd "$(dirname "$0")"
# Where the .PRG lands. The build directory when CMake drives us (it exports
# MFC_OUT); the script's own directory when run by hand. It is a build output
# either way -- not committed -- so nothing downstream may assume it is here.
OUT="${MFC_OUT:-.}"
mkdir -p "$OUT"


cl65 -t none --signed-chars -O -C venture.cfg \
    venture.c glue.s -o venture.bin

# Prepend the 2-byte little-endian $0800 load header -> a .PRG.
printf '\000\010' > "$OUT/VENTURE.PRG"
cat venture.bin >> "$OUT/VENTURE.PRG"
echo "Wrote VENTURE.PRG ($(wc -c < "$OUT/VENTURE.PRG" | tr -d ' ') bytes)"
