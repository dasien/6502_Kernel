#!/usr/bin/env python3
"""Generate the MFC FORTH bank-module source from the byte-verified figforth.s.

figforth.s assembles byte-identical to the original listing at $0200 (proven by
check.sh).  This script applies the minimal, size-preserving patches needed to
run it as a ROM bank module in the $B000-$DFFF window:

  * ORIG $0200 -> $B000               (linker + COLD's ORIG+n boot-param reads)
  * initial FENCE/DP -> $0800 (RAM)   so new words compile to RAM, not ROM
  * NOP the two single-step debug calls (JSR TRACE in NEXT, JSR TCOLON in DOCOL)
  * XQTER (?TERMINAL) -> push 0        (no System-65 break port here)
  * MON word body -> JMP $FF12         (K_RETURN_MODULE: exit Forth to the DOS)
  * OUTCH/INCH/TCR equates -> thunks   to the kernel $FF00 jump table

Every in-image patch preserves byte count, so the pre-compiled dictionary's
absolute addresses stay self-consistent after relocation.  The I/O thunks are
appended after TOP (free space inside the 12 KB window).
"""
import sys

SRC = sys.argv[1] if len(sys.argv) > 1 else 'figforth.s'
OUT = sys.argv[2] if len(sys.argv) > 2 else '../../src/kernel/forth/forth.s'

lines = open(SRC).read().split('\n')


def expect(idx, needle):
    if needle not in lines[idx]:
        raise SystemExit(f'PATCH ANCHOR MISMATCH at line {idx+1}: '
                         f'expected {needle!r}, got {lines[idx]!r}')


# 1-based listing line numbers from figforth.s (see grep map).
expect(55, 'ORIG = $0200')
lines[55] = 'ORIG = $B000   ; module window origin (was $0200)'

expect(62, 'OUTCH = $D2C1')
lines[62] = '; OUTCH/INCH/TCR redefined as kernel-service thunks (end of file)'
expect(63, 'INCH = $D1DC')
lines[63] = ';'
expect(64, 'TCR = $D0F1')
lines[64] = ';'

expect(94, '.WORD TOP')
lines[94] = '.WORD $0802   ; Initial fence (RAM; $0800 holds FORTHLATEST thread cell)'
expect(95, '.WORD TOP')
lines[95] = '.WORD $0802   ; Initial DP/top-of-dict (new words compile into RAM)'

expect(148, 'JSR TRACE')
lines[148] = '.byte $EA,$EA,$EA   ; was JSR TRACE (single-step debug disabled)'
expect(1247, 'JSR TCOLON')
lines[1247] = '.byte $EA,$EA,$EA   ; was JSR TCOLON (single-step debug disabled)'

# ROMable-vocab fix: the FORTH vocabulary's thread cell (XFOR = FORTH+6) is now
# in ROM, so CREATE's "store new NFA into CURRENT @" and COLD's init both target
# ROM and are ignored, orphaning every new definition.  Redirect the FORTH vocab
# to a RAM thread cell (FORTHLATEST = $0800): give it a private does-routine
# DOVOCF that points CONTEXT at FORTHLATEST, and have COLD seed FORTHLATEST=NTOP.
# (User-created vocabularies are unaffected: their thread cell is already in RAM.)
expect(3220, '.WORD DOVOC')
lines[3220] = '.WORD DOVOCF   ; FORTH uses a RAM thread cell (ROMable-vocab fix)'
expect(3322, 'STA FORTH+6')
lines[3322] = 'STA FORTHLATEST     ; seed RAM thread cell (was STA FORTH+6, ROM)'
expect(3324, 'STA FORTH+7')
lines[3324] = 'STA FORTHLATEST+1   ; (was STA FORTH+7, ROM)'

# XQTER: original LDA $C000 / CMP $C001 / AND #1  == 8 bytes -> LDA #0 + 6 NOP
expect(4033, 'XQTER: LDA $C000')
lines[4033] = 'XQTER: .byte $A9,$00   ; LDA #0  (?TERMINAL: no break key on this host)'
expect(4034, 'CMP $C001')
lines[4034] = '.byte $EA,$EA,$EA'
expect(4035, 'AND #1')
lines[4035] = '.byte $EA,$EA,$EA'

# MON body: STX XSAVE / BRK / LDX XSAVE / JMP NEXT == 8 bytes -> JMP $FF12 + 5 NOP
expect(4844, 'MON: .WORD *+2')
expect(4845, 'STX XSAVE')
lines[4845] = '.byte $4C,$12,$FF   ; JMP $FF12 = K_RETURN_MODULE (exit Forth -> DOS)'
expect(4846, 'BRK')
lines[4846] = '.byte $EA,$EA'
expect(4847, 'LDX XSAVE')
lines[4847] = '.byte $EA,$EA'
expect(4848, 'JMP NEXT')
lines[4848] = '.byte $EA'

THUNKS = """
; ================================================================
;  MFC kernel I/O thunks  (replace the Rockwell System-65 OUTCH/INCH/TCR).
;  XEMIT/XKEY/XCR call through these, so only the equates change.
;  Y is preserved: the inner interpreter keeps Y=0 for (IP),Y, but the
;  kernel PRINT routines may clobber it.
; ================================================================
K_PRINT_CHAR = $FF00
K_PRINT_NL   = $FF06
K_GET_KEY    = $FF09

OUTCH:  PHY
        JSR K_PRINT_CHAR        ; A = character to emit
        PLY
        RTS

TCR:    PHY
        JSR K_PRINT_NL
        PLY
        RTS

INCH:   JSR K_GET_KEY           ; non-blocking; C set + A = key when ready
        BCC INCH                ; spin until a key arrives -> blocking read
        RTS

; ================================================================
;  ROMable-vocabulary support.
;  FORTHLATEST is the FORTH vocabulary's "latest word" thread cell, relocated
;  to RAM ($0800; the dictionary proper starts at $0802).  COLD seeds it with
;  NTOP.  DOVOCF is FORTH's private vocabulary does-routine: it drops the PFA
;  pushed by DODOE and stores FORTHLATEST into CONTEXT, so dictionary search and
;  new definitions key off a writable RAM cell instead of the ROM XFOR field.
; ================================================================
FORTHLATEST = $0800

DOVOCF: .WORD DROP              ; discard PFA pushed by DODOE
        .WORD LIT
        .WORD FORTHLATEST       ; inline literal: address of the RAM thread cell
        .WORD CON               ; CONTEXT user variable
        .WORD STORE             ; CONTEXT ! FORTHLATEST
        .WORD SEMIS
"""

HEADER = ('; forth.s - FIG-Forth 6502 (Ragsdale Rel 1.1) as an MFC bank module.\n'
          '; GENERATED from vendor/fig-forth/figforth.s by make_module.py - do not\n'
          '; hand-edit; change the converter/patcher instead. The pristine listing\n'
          '; and the byte-identical $0200 build live under vendor/fig-forth/.\n'
          '.setcpu "65C02"\n\n')

out = HEADER + '\n'.join(lines).rstrip('\n') + '\n' + THUNKS
open(OUT, 'w').write(out)
print(f'wrote {OUT}')
