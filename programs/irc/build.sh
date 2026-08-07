#!/bin/sh
# Build IRC.PRG -- a minimal IRC chat client over the modem (telnet TCP).
# Requires cc65 (`brew install cc65`). --signed-chars per our convention.
set -e
cd "$(dirname "$0")"

cl65 -t none --signed-chars -O -I ../common -C irc.cfg irc.c ../common/scrollback.c glue.s -o irc.bin

# Prepend the 2-byte little-endian $0800 load header -> a .PRG.
printf '\000\010' > IRC.PRG
cat irc.bin >> IRC.PRG
echo "Wrote IRC.PRG ($(wc -c < IRC.PRG | tr -d ' ') bytes)"
