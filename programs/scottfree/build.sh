#!/bin/sh
# Build a Scott Adams adventure as a self-contained MFC-DOS .PRG.
#
# Pipeline (host pre-parse -> embed -> cc65 -> .PRG):
#   1. dat2c  <game>.dat      -> <name>_data.c   (tables; no runtime parser)
#   2. cl65   scott.c + data + glue.s -> <name>.bin
#   3. prepend the 2-byte $0800 load header      -> <NAME>.PRG
#
# Requires the cc65 toolchain (`brew install cc65`) and a host C compiler.
# --signed-chars is REQUIRED (cc65 defaults to unsigned; the engine needs
# signed char), matching the chess port.
#
# Usage:  ./build.sh ~/Downloads/AdamsGames/adv01.dat ADV01
set -e
cd "$(dirname "$0")"

DAT="${1:?usage: build.sh <game.dat> <OUTNAME>}"
OUT="${2:?usage: build.sh <game.dat> <OUTNAME>}"

# 1. host tool: pre-parse the database into C initializers
cc -O2 -o dat2c dat2c.c
./dat2c "$DAT" > game_data.c

# 2. compile engine + generated data + glue for the none target
cl65 -t none --signed-chars -O -C scott.cfg scott.c game_data.c glue.s -o game.bin

# 3. prepend the little-endian $0800 load-address header
printf '\000\010' > "$OUT.PRG"
cat game.bin >> "$OUT.PRG"
echo "Wrote $OUT.PRG ($(wc -c < "$OUT.PRG" | tr -d ' ') bytes)"
