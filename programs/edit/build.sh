#!/bin/sh
# Build EDIT.PRG -- the minimal full-screen editor spike (kilo-inspired).
# Requires cc65 (`brew install cc65`). --signed-chars per our convention.
set -e
cd "$(dirname "$0")"
# Where the .PRG lands. The build directory when CMake drives us (it exports
# MFC_OUT); the script's own directory when run by hand. It is a build output
# either way -- not committed -- so nothing downstream may assume it is here.
OUT="${MFC_OUT:-.}"
mkdir -p "$OUT"


cl65 -t none --signed-chars -O -C edit.cfg edit.c glue.s -o edit.bin

# Prepend the 2-byte little-endian $0800 load header -> a .PRG.
printf '\000\010' > "$OUT/EDIT.PRG"
cat edit.bin >> "$OUT/EDIT.PRG"
echo "Wrote EDIT.PRG ($(wc -c < "$OUT/EDIT.PRG" | tr -d ' ') bytes)"
