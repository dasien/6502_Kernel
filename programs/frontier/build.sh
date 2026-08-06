#!/bin/sh
# Build FRONTIER.PRG -- Frontier Fortune trading game (cc65). --signed-chars per convention.
set -e
cd "$(dirname "$0")"

cl65 -t none --signed-chars -O -C frontier.cfg \
    frontier.c glue.s -o frontier.bin

# Prepend the 2-byte little-endian $0800 load header -> a .PRG.
printf '\000\010' > FRONTIER.PRG
cat frontier.bin >> FRONTIER.PRG
echo "Wrote FRONTIER.PRG ($(wc -c < FRONTIER.PRG | tr -d ' ') bytes)"
