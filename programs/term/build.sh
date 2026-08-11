#!/bin/sh
# Build TERM.PRG -- the serial ANSI/VT100 terminal + telnet BBS dialer.
# Requires cc65 (`brew install cc65`). --signed-chars per our convention.
set -e
cd "$(dirname "$0")"
# Where the .PRG lands. The build directory when CMake drives us (it exports
# MFC_OUT); the script's own directory when run by hand. It is a build output
# either way -- not committed -- so nothing downstream may assume it is here.
OUT="${MFC_OUT:-.}"
mkdir -p "$OUT"


cl65 -t none --signed-chars -O -I ../common -C term.cfg term.c ../common/scrollback.c glue.s -o term.bin

# Prepend the 2-byte little-endian $0800 load header -> a .PRG.
printf '\000\010' > "$OUT/TERM.PRG"
cat term.bin >> "$OUT/TERM.PRG"
echo "Wrote TERM.PRG ($(wc -c < "$OUT/TERM.PRG" | tr -d ' ') bytes)"
