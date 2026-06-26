#!/usr/bin/env python3
"""Reconstruct the reference fig-FORTH 6502 binary from the assembler listing.

The listing columns are fixed-width:
    [0:4]   LINE#         (ignored)
    [6:10]  LOC           (4 hex digits, the location counter)
    [12:22] CODE          (up to 3 object bytes "XX XX XX")
    [22:]   SOURCE        (ignored here)

Multi-byte data spills onto continuation lines that repeat the LINE# but
advance LOC and carry only CODE bytes.  Equate/comment lines show LOC 0000
(or the current PC) with a blank CODE field and emit nothing.

We collect addr->byte, detect conflicts, and write a contiguous image from
ORIG to the highest emitted address, filling any gaps with $00.
"""
import re
import sys

ORIG = 0x0200
LOC_RE = re.compile(r'^[0-9A-Fa-f]{4}$')
BYTE_RE = re.compile(r'^[0-9A-Fa-f]{2}$')


def main(path, out_bin):
    mem = {}
    conflicts = []
    with open(path, 'r', errors='replace') as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.rstrip('\n')
            if len(line) < 12:
                continue
            loc_field = line[6:10]
            if not LOC_RE.match(loc_field):
                continue
            code_field = line[12:22]
            toks = code_field.split()
            if not toks or not all(BYTE_RE.match(t) for t in toks):
                continue
            loc = int(loc_field, 16)
            for i, t in enumerate(toks):
                addr = loc + i
                b = int(t, 16)
                if addr in mem and mem[addr] != b:
                    conflicts.append((addr, mem[addr], b, lineno))
                mem[addr] = b

    if not mem:
        print("ERROR: no bytes parsed", file=sys.stderr)
        return 1

    lo = min(mem)
    hi = max(mem)
    print(f"emitted range: ${lo:04X}-${hi:04X}  ({hi-lo+1} bytes span, {len(mem)} bytes set)")
    if lo != ORIG:
        print(f"NOTE: lowest addr ${lo:04X} != ORIG ${ORIG:04X}")

    # gaps
    gaps = []
    run_start = None
    for a in range(lo, hi + 1):
        if a not in mem:
            if run_start is None:
                run_start = a
        else:
            if run_start is not None:
                gaps.append((run_start, a - 1))
                run_start = None
    if run_start is not None:
        gaps.append((run_start, hi))
    if gaps:
        print(f"gaps (filled with $00): {len(gaps)}")
        for g0, g1 in gaps:
            print(f"   ${g0:04X}-${g1:04X}  ({g1-g0+1} bytes)")
    if conflicts:
        print(f"CONFLICTS: {len(conflicts)}")
        for addr, old, new, ln in conflicts[:20]:
            print(f"   ${addr:04X}: ${old:02X} vs ${new:02X} at listing-file line {ln}")

    img = bytearray(hi - lo + 1)
    for a, b in mem.items():
        img[a - lo] = b
    with open(out_bin, 'wb') as o:
        o.write(img)
    print(f"wrote {out_bin}: {len(img)} bytes (org ${lo:04X})")
    return 0


if __name__ == '__main__':
    src = sys.argv[1] if len(sys.argv) > 1 else 'figforth-6502-r1.1.lst.txt'
    out = sys.argv[2] if len(sys.argv) > 2 else 'figforth-ref.bin'
    sys.exit(main(src, out))
