; ============================================================================
; romid.inc -- the identity block at the top of every MFC ROM
; ============================================================================
; Sixteen bytes in the ROM's own last line, so the kernel can say at boot what
; is installed without any table of its own: it maps each module bank in turn,
; looks for the marker at $EFF0, and prints the name and size it finds. The DOS
; ROM carries one at $AFF0. Last rather than first because a module's entry
; points live at $B000 and the DOS's jump table at $AF00 -- nothing moves.
;
;   +0   "MFC"        marker: an empty or foreign bank does not have it
;   +3   format       ROMID_FORMAT; bump it if this layout ever changes
;   +4   name         8 characters, padded with spaces
;   +12  size         the ROM's size in bytes, 32-bit little-endian -- PRINT_DEC's
;                     operand shape, so the kernel prints it where it stands
;
; One per ROM, from a three-line file in src/kernel/romid/ linked into it, so a
; generated source (forth.s) needs no hand edit. The ROM's .cfg places the
; ROMID segment at its last 16 bytes.
; ============================================================================

ROMID_FORMAT = $01

.macro ROM_ID name, size
    .segment "ROMID"
    .byte "MFC", ROMID_FORMAT
    .byte name
    .res 8 - .strlen(name), ' '
    .dword size
.endmacro
