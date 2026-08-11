#!/bin/sh
# Build IRC.PRG -- a minimal IRC chat client over the modem (telnet TCP).
# Requires cc65 (`brew install cc65`). --signed-chars per our convention.
set -e
cd "$(dirname "$0")"
# Where the .PRG lands. The build directory when CMake drives us (it exports
# MFC_OUT); the script's own directory when run by hand. It is a build output
# either way -- not committed -- so nothing downstream may assume it is here.
OUT="${MFC_OUT:-.}"
mkdir -p "$OUT"


cl65 -t none --signed-chars -O -I ../common -C irc.cfg irc.c ../common/scrollback.c glue.s -o irc.bin

# Prepend the 2-byte little-endian $0800 load header -> a .PRG.
printf '\000\010' > "$OUT/IRC.PRG"
cat irc.bin >> "$OUT/IRC.PRG"
echo "Wrote IRC.PRG ($(wc -c < "$OUT/IRC.PRG" | tr -d ' ') bytes)"
