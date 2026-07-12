#!/bin/sh
# Build VAULT.PRG -- The Sunless Vault roguelike (cc65). --signed-chars per convention.
set -e
cd "$(dirname "$0")"

cl65 -t none --signed-chars -O -C vault.cfg \
    vault.c map.c draw.c player.c monster.c combat.c item.c spell.c data.c glue.s -o vault.bin

# Prepend the 2-byte little-endian $0800 load header -> a .PRG.
printf '\000\010' > VAULT.PRG
cat vault.bin >> VAULT.PRG
echo "Wrote VAULT.PRG ($(wc -c < VAULT.PRG | tr -d ' ') bytes)"
