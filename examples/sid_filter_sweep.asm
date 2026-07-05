; SID filter sweep demo for MFC 6502
;
; Plays a ~440 Hz sawtooth on voice 1 of the SID and sweeps the low-pass
; filter cutoff up and down -- the classic "SID sweep". Press any key to
; stop the tone and return to the monitor.
;
; Written for the built-in two-pass assembler (devtools "B" command):
; UPPERCASE directives/labels, identifiers <= 8 chars, no underscores.
; Load it with "L" (host file picker), then build with "B".
;
; The SID is the real 6581/8580 register layout relocated to $FE38-$FE54.

.ORG $0800

; --- SID registers (voice 1 + filter) ---
FREQLO  = $FE38         ; voice 1 frequency low
FREQHI  = $FE39         ; voice 1 frequency high
CTRL    = $FE3C         ; control: bit0 gate, bit5 sawtooth
AD      = $FE3D         ; attack (hi nibble) / decay (lo nibble)
SR      = $FE3E         ; sustain (hi nibble) / release (lo nibble)
FCLO    = $FE4D         ; filter cutoff low (bits 0-2)
FCHI    = $FE4E         ; filter cutoff high (8 bits) -- swept below
RESFILT = $FE4F         ; resonance (hi nibble) + voice routing (lo nibble)
MODEVOL = $FE50         ; filter mode (hi nibble) + master volume (lo nibble)

; --- Keyboard (PIA) ---
PIADATA = $FE00         ; read consumes a key
PIACTRL = $FE02         ; bit 0 = a key is available

START:
    ; Voice 1: ~440 Hz, sawtooth, gate on, instant attack, full sustain.
    LDA #$D6
    STA FREQLO
    LDA #$1C
    STA FREQHI          ; $1CD6 -> ~440 Hz
    LDA #$00
    STA AD              ; attack=0, decay=0
    LDA #$F0
    STA SR              ; sustain=15, release=0
    LDA #$21
    STA CTRL            ; sawtooth ($20) + gate ($01)

    ; Filter: route voice 1, resonance 8, low-pass, full volume.
    LDA #$81
    STA RESFILT         ; resonance nibble 8, route voice 1
    LDA #$1F
    STA MODEVOL         ; low-pass ($10) + volume 15 ($0F)
    LDA #$00
    STA FCLO            ; cutoff low bits = 0

SWEEPUP:
    LDX #$00            ; cutoff 0 (dark) -> 255 (bright)
UP:
    STX FCHI
    JSR DELAY
    JSR KEYPRES
    BCS STOP
    INX
    BNE UP

    LDX #$FF            ; sweep back down
DOWN:
    STX FCHI
    JSR DELAY
    JSR KEYPRES
    BCS STOP
    DEX
    BNE DOWN
    BRA SWEEPUP         ; loop until a key is pressed

STOP:
    LDA #$00
    STA CTRL            ; gate off
    STA MODEVOL         ; volume 0 (silence)
    LDA PIADATA         ; consume the key
    RTS

; KEYPRES - carry set if a key is waiting.
KEYPRES:
    LDA PIACTRL
    AND #$01
    BEQ NOKEY
    SEC
    RTS
NOKEY:
    CLC
    RTS

; DELAY - short busy-wait (~6 ms). Preserves X (the current cutoff).
DELAY:
    PHX
    LDX #$06
DLY1:
    LDY #$00
DLY2:
    DEY
    BNE DLY2
    DEX
    BNE DLY1
    PLX
    RTS

    .END

; ---------------------------------------------------------------------------
; No assembler? Enter it by hand in the monitor instead:
;   1. W:0800    <Enter>   then paste the bytes below, <Enter> on a blank line
;   2. G:0800    <Enter>   run; press any key to stop
;
; Machine code (110 bytes, 65C02):
;
; A9 D6 8D 38 FE A9 1C 8D 39 FE A9 00 8D 3D FE A9
; F0 8D 3E FE A9 21 8D 3C FE A9 81 8D 4F FE A9 1F
; 8D 50 FE A9 00 8D 4D FE A2 00 8E 4E FE 20 61 08
; 20 56 08 B0 15 E8 D0 F2 A2 FF 8E 4E FE 20 61 08
; 20 56 08 B0 05 CA D0 F2 80 DE A9 00 8D 3C FE 8D
; 50 FE AD 00 FE 60 AD 02 FE 29 01 F0 02 38 60 18
; 60 DA A2 06 A0 00 88 D0 FD CA D0 F8 FA 60
