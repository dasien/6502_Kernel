#!/bin/sh
# Build CHESS.PRG -- H.G. Muller's micro-Max 1.6 ported to MFC-DOS.
#
# Requires the cc65 toolchain on PATH (`brew install cc65`).
#   - none-target runtime + custom ld65 config (micromax.cfg)
#   - glue.s maps the kernel console ABI onto cc65's OUTCH/INCH/CLS
#   - --signed-chars is REQUIRED: micro-Max assumes signed char; cc65
#     defaults to unsigned, which silently breaks move generation.
set -e
cd "$(dirname "$0")"

cl65 -t none --signed-chars -O -C micromax.cfg umax_mfc.c glue.s -o micromax.bin

# Prepend the 2-byte little-endian $0800 load-address header -> a .PRG the
# DOS can launch by name.
printf '\000\010' > CHESS.PRG
cat micromax.bin >> CHESS.PRG
echo "Wrote CHESS.PRG ($(wc -c < CHESS.PRG | tr -d ' ') bytes)"
