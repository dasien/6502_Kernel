#!/bin/sh
# Build TERM.PRG -- the serial ANSI/VT100 terminal + telnet BBS dialer.
# Requires cc65 (`brew install cc65`). --signed-chars per our convention.
set -e
cd "$(dirname "$0")"

cl65 -t none --signed-chars -O -I ../common -C term.cfg term.c ../common/scrollback.c glue.s -o term.bin

# Prepend the 2-byte little-endian $0800 load header -> a .PRG.
printf '\000\010' > TERM.PRG
cat term.bin >> TERM.PRG
echo "Wrote TERM.PRG ($(wc -c < TERM.PRG | tr -d ' ') bytes)"
