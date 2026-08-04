#!/bin/sh
# Build VENTURE.PRG -- VENTURE (Exidy 1981 port) (cc65). --signed-chars per convention.
set -e
cd "$(dirname "$0")"

cl65 -t none --signed-chars -O -C venture.cfg \
    venture.c glue.s -o venture.bin

# Prepend the 2-byte little-endian $0800 load header -> a .PRG.
printf '\000\010' > VENTURE.PRG
cat venture.bin >> VENTURE.PRG
echo "Wrote VENTURE.PRG ($(wc -c < VENTURE.PRG | tr -d ' ') bytes)"
