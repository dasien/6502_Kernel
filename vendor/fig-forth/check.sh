#!/bin/sh
set -e
ca65 --cpu 6502 -o figforth.o figforth.s
ld65 -C figforth-check.cfg -o figforth-out.bin figforth.o
echo "out: $(wc -c < figforth-out.bin) bytes  ref: $(wc -c < figforth-ref.bin) bytes"
if cmp -s figforth-out.bin figforth-ref.bin; then
    echo "BYTE-IDENTICAL ✓"
else
    echo "DIFFERS:"
    cmp -l figforth-out.bin figforth-ref.bin | head -40
    echo "(diff count: $(cmp -l figforth-out.bin figforth-ref.bin | wc -l))"
fi
