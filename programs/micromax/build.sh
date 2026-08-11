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
# Where the .PRG lands. The build directory when CMake drives us (it exports
# MFC_OUT); the script's own directory when run by hand. It is a build output
# either way -- not committed -- so nothing downstream may assume it is here.
OUT="${MFC_OUT:-.}"
mkdir -p "$OUT"


cl65 -t none --signed-chars -O -C micromax.cfg umax_mfc.c glue.s -o micromax.bin

# Prepend the 2-byte little-endian $0800 load-address header -> a .PRG the
# DOS can launch by name.
printf '\000\010' > "$OUT/CHESS.PRG"
cat micromax.bin >> "$OUT/CHESS.PRG"
echo "Wrote CHESS.PRG ($(wc -c < "$OUT/CHESS.PRG" | tr -d ' ') bytes)"
