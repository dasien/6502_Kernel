#!/bin/sh
# Build EDIT.PRG -- the minimal full-screen editor spike (kilo-inspired).
# Requires cc65 (`brew install cc65`). --signed-chars per our convention.
set -e
cd "$(dirname "$0")"

cl65 -t none --signed-chars -O -C edit.cfg edit.c glue.s -o edit.bin

# Prepend the 2-byte little-endian $0800 load header -> a .PRG.
printf '\000\010' > EDIT.PRG
cat edit.bin >> EDIT.PRG
echo "Wrote EDIT.PRG ($(wc -c < EDIT.PRG | tr -d ' ') bytes)"
