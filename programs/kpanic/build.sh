#!/bin/sh
# Build KPANIC.PRG -- KERNEL PANIC vertical scroller (cc65). --signed-chars per convention.
set -e
cd "$(dirname "$0")"

cl65 -t none --signed-chars -O -C kpanic.cfg \
    kpanic.c glue.s -o kpanic.bin

# Prepend the 2-byte little-endian $0800 load header -> a .PRG.
printf '\000\010' > KPANIC.PRG
cat kpanic.bin >> KPANIC.PRG
echo "Wrote KPANIC.PRG ($(wc -c < KPANIC.PRG | tr -d ' ') bytes)"
