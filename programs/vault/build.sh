#!/bin/sh
# Build VAULT.PRG -- The Sunless Vault roguelike (cc65). --signed-chars per convention.
set -e
cd "$(dirname "$0")"
# Where the .PRG lands. The build directory when CMake drives us (it exports
# MFC_OUT); the script's own directory when run by hand. It is a build output
# either way -- not committed -- so nothing downstream may assume it is here.
OUT="${MFC_OUT:-.}"
mkdir -p "$OUT"


cl65 -t none --signed-chars -O -C vault.cfg \
    vault.c map.c draw.c player.c monster.c combat.c item.c spell.c data.c glue.s -o vault.bin

# Prepend the 2-byte little-endian $0800 load header -> a .PRG.
printf '\000\010' > "$OUT/VAULT.PRG"
cat vault.bin >> "$OUT/VAULT.PRG"
echo "Wrote VAULT.PRG ($(wc -c < "$OUT/VAULT.PRG" | tr -d ' ') bytes)"
