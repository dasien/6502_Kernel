#!/usr/bin/env python3
"""Convert Daryl Rictor's XMODEM/CRC (vendor/xmodem/xmodem.txt) to a ca65 source
for the MFC spike: retarget its 6551 driver to our emulated ACIA ($FE29-$FE2C),
relocate the program + CRC tables into free RAM at $2000 (the original $FA00/
$FD00/$FE00 origins collide with our kernel ROM / I/O page), and fix a couple of
ca65 syntax nits. The label-case inconsistencies in the original are handled by
assembling with `ca65 --ignore-case`.
"""
import re
import sys

SRC = sys.argv[1] if len(sys.argv) > 1 else 'xmodem.txt'
OUT = sys.argv[2] if len(sys.argv) > 2 else 'xmodem.s'

HEADER = (
    "; GENERATED from vendor/xmodem/xmodem.txt by make_xmodem.py - do not hand-edit.\n"
    "; Daryl Rictor's XMODEM/CRC for the 65C02, retargeted to the MFC 6551 ACIA\n"
    "; and relocated to $2000 for the headless spike harness. Assemble with\n"
    "; `ca65 --ignore-case` (the original mixes label case).\n"
    ".setcpu \"65C02\"\n"
    ".feature labels_without_colons\n"
    "\n"
)

lines = open(SRC, errors='replace').read().split('\n')
out = []
for ln in lines:
    # Retarget the 6551 ACIA registers to our memory-mapped ACIA.
    if re.match(r'\s*ACIA_Data\s*=', ln):
        out.append('ACIA_Data\t=\t$FE29\t; MFC emulated 6551')
        continue
    if re.match(r'\s*ACIA_Status\s*=', ln):
        out.append('ACIA_Status\t=\t$FE2A')
        continue
    if re.match(r'\s*ACIA_Command\s*=', ln):
        out.append('ACIA_Command\t=\t$FE2B')
        continue
    if re.match(r'\s*ACIA_Control\s*=', ln):
        out.append('ACIA_Control\t=\t$FE2C')
        continue
    # Relocate the three origin directives into free RAM, page-aligning tables.
    if re.match(r'\s*\*=\s*\$FA00', ln):
        out.append('\t\t; program origin set by the linker (was $FA00)')
        continue
    if re.match(r'\s*\*=\s*\$FD00', ln):
        out.append('\t\t.align\t256\t\t; crclo table (was $FD00)')
        continue
    if re.match(r'\s*\*=\s*\$FE00', ln):
        out.append('\t\t.align\t256\t\t; crchi table (was $FE00)')
        continue
    # ca65 wants char immediates in single quotes, not double.
    ln = re.sub(r'#"(.)"', r"#'\1'", ln)
    out.append(ln)

open(OUT, 'w').write(HEADER + '\n'.join(out) + '\n')
print(f'wrote {OUT}')
