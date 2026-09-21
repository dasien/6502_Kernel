; ============================================================================
; ScottFree on MFC-DOS -- cc65 runtime glue
; ============================================================================
;   void OUTCH(char c)  -- print a char (LF -> newline)
;   char INCH(void)     -- blocking raw key (ESC -> quit to DOS); no echo
;   void CLS(void)      -- clear screen + home
;   int  RND(void)      -- 16-bit Galois LFSR
;   void QUITDOS(void)  -- return to the DOS ] prompt
; cc65 convention: char arg in A; char/int result in A / A:X.
; ============================================================================

.export _INCH, _QUITDOS
.export _dir_first, _dir_next

.include "mfc.inc"

.importzp ptr1                  ; cc65 zero-page scratch (above the DOS's $14-$3F)


.PC02                           ; WDC 65C02, as the kernel, monitor and DOS declare.
                                ; Stated here as well as on the ca65 command line so
                                ; the file is right however it is assembled.

.segment "CODE"

; ---- char INCH(void) -- raw key in A (X=0); ESC exits to DOS ----------------
.proc _INCH
@wait:  jsr     K_GET_KEYSTROKE
        bcc     @wait
        cmp     #$1B            ; ESC -> back to the DOS prompt
        beq     _QUITDOS
        ldx     #$00
        rts
.endproc

; ---- void QUITDOS(void) ----------------------------------------------------
.proc _QUITDOS
        jsr     K_PRINT_NEWLINE
        jmp     DOS_WARM
.endproc

; ---- char dir_first(char *dst11) / char dir_next(char *dst11) --------------
; Enumerate root-dir entries: copy the 11-byte 8.3 name into dst; returns
; 0 = got an entry, 1 = no more. Run to completion before opening a file.
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
