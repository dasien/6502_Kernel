#!/bin/sh
# Build the twelve Adventure International (Scott Adams) games as named
# MFC-DOS .PRGs, via build.sh (host pre-parse -> cc65 -> .PRG).
#
# The game .dat databases are shareware and are NOT redistributed in this
# repo. Point this at a directory containing adv01.dat .. adv12.dat.
#
# Usage:  ./build_games.sh <dir-with-advNN.dat> [outdir]
#         ./build_games.sh ~/Downloads/AdamsGames
set -e
cd "$(dirname "$0")"
DIR="${1:?usage: build_games.sh <dat-dir> [outdir]}"
OUT="${2:-.}"

# advNN  ->  short DOS (8.3) program name
for pair in \
    01:ADVLAND  02:PIRATE   03:SECRET   04:VOODOO \
    05:COUNT    06:ODYSSEY  07:FUNHOUSE 08:PYRAMID \
    09:GHOSTTWN 10:SAVAGE1  11:SAVAGE2  12:VOYAGE
do
    num=${pair%%:*}
    name=${pair#*:}
    ./build.sh "$DIR/adv$num.dat" "$name" >/dev/null
    [ "$OUT" = "." ] || mv "$name.PRG" "$OUT/"
    echo "  built $name.PRG  (adv$num)"
done
echo "Done -> $OUT"
