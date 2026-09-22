; ============================================================================
; dir.s -- cc65 glue for enumerating a directory
; ============================================================================
; Part of libmfcglue; see kernel.s for how the library is linked.
;
; The DOS exposes enumeration as first/next over a single shared cursor, so
; there is one walk at a time and it has to run to completion before anything
; opens a file -- opening moves the same state. A caller that wants to act on
; what it found collects the names first, then acts.
; ============================================================================

.export _dir_first, _dir_next

.include "mfc.inc"
.importzp ptr1                  ; cc65 zero-page scratch

.PC02
.segment "CODE"

; char dir_first(char *dst11) / char dir_next(char *dst11)
; Copy the 11-byte 8.3 name of the next entry into dst (A/X = dst).
; Returns 0 = got an entry, 1 = no more.
_dir_first:
        sta     ptr1
        stx     ptr1+1
        jsr     FS_DIR_FIRST
        jmp     dir_copy
_dir_next:
        sta     ptr1
        stx     ptr1+1
        jsr     FS_DIR_NEXT
dir_copy:
        bcs     dir_none
        ldy     #$0A            ; copy DOS_ENTRY[0..10] -> (ptr1)
@cp:    lda     DOS_ENTRY,y
        sta     (ptr1),y
        dey
        bpl     @cp
        lda     #$00
        ldx     #$00
        rts
dir_none:
        lda     #$01
        ldx     #$00
        rts
