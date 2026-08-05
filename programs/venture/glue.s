; ============================================================================
; VENTURE -- cc65 runtime glue
; ============================================================================
; The 80x25 screen lives behind the VIC register port (not in the 64K map), so
; all drawing goes through the video helpers: set the cell index with vaddr(),
; then stream glyphs with vputc() (the port auto-increments).
;
; Lifted from KERNEL PANIC's glue, minus vscrollbot(): Venture never scrolls, it
; redraws the cells that changed. What it does share is the pair that make
; real-time play possible on this machine --
;   jiffies()  -- the kernel's 60 Hz monotonic tick counter (K_GET_JIFFIES),
;                 which paces the fixed-tick loop.
;   keystate() -- the live held-key bitmask at $FE0F. Eight-way movement while
;                 firing is impossible from the keystroke FIFO: it carries no
;                 key-up, and the host only auto-repeats the most recent key.
; ============================================================================

.export _INCH_NB, _QUITDOS, _keystate
.export _vaddr, _vputc, _vattr
.export _vfill, _vcmd, _vhidecur
.export _rng_seed, _rtc_sec, _jiffies
.export _sound_tone, _sound_off

K_GET_KEYSTROKE = $FF09         ; non-blocking: C set + A=char
K_CLEAR_SCREEN  = $FF0C         ; clear + home (also resets the kernel cursor)
K_GET_JIFFIES   = $FF39         ; 60 Hz monotonic counter -> A=lo, X=hi
K_SOUND_TONE    = $FF33         ; play a tone on SID voice 1 (A=freq lo, X=freq hi)
K_SOUND_OFF     = $FF36         ; gate voice 1 off
DOS_WARM        = $AF1E

; PIA live key-state port: which control keys are held RIGHT NOW. Distinct from
; the keystroke buffer at $FE00, which records what was typed and carries no
; key-up -- polling that can't tell held from released, and the host OS only
; auto-repeats the most recent key, so "move while firing" is impossible there.
KEY_STATE       = $FE0F

; VIC video register port (chars/colors live behind these, not in the map).
VREG_ADDR_LO    = $FE2D         ; cell index low (0..1999)
VREG_ADDR_HI    = $FE2E         ; cell index high
VREG_CHAR       = $FE2F         ; char data port; full 8-bit glyph; auto-increments
VREG_ATTR       = $FE31         ; attribute latch [R][BR][bg:3][fg:3]
VREG_CMD        = $FE32         ; 1=clear 2=scroll-up 3=scroll-down 4=fill-row
VREG_CURSOR_HI  = $FE35         ; cursor cell high; bit7 = hidden
VREG_CMD_PARAM  = $FE36         ; fill char for clear / fill-row / scroll

; RTC (real-time clock) -- entropy source and a 1 Hz tick for the rate readout.
RTC_LATCH       = $FE55         ; write snapshots the live clock into the read regs
RTC_SEC         = $FE56         ; BCD seconds
RTC_MIN         = $FE57         ; BCD minutes
RTC_HOUR        = $FE58         ; BCD hours
RTC_FATTIME_LO  = $FE5D         ; host-packed FAT time low (sub-second-ish bits)
RTC_FATTIME_HI  = $FE5E         ; host-packed FAT time high

.segment "CODE"

; unsigned int jiffies(void) -- read the kernel's 60 Hz monotonic tick counter.
; K_GET_JIFFIES already returns A=low / X=high, which is exactly cc65's 16-bit
; return convention, so this is a straight tail call. The counter wraps every
; ~18.2 minutes; callers compare deltas with unsigned subtraction so that's fine.
.proc _jiffies
        jmp     K_GET_JIFFIES
.endproc

; void vaddr(unsigned int cell) -- point the data port at a cell (A=lo, X=hi).
.proc _vaddr
        sta     VREG_ADDR_LO
        stx     VREG_ADDR_HI
        rts
.endproc

; void vattr(unsigned char a) -- set the color/attribute latch for next writes.
.proc _vattr
        sta     VREG_ATTR
        rts
.endproc

; void vputc(unsigned char ch) -- write a glyph at the current cell and advance.
.proc _vputc
        sta     VREG_CHAR
        rts
.endproc

; void vfill(unsigned char ch) -- set the fill char used by clear/fill-row/scroll.
.proc _vfill
        sta     VREG_CMD_PARAM
        rts
.endproc

; void vcmd(unsigned char cmd) -- run a chip-side block op (clear/scroll/fill-row).
.proc _vcmd
        sta     VREG_CMD
        rts
.endproc

; void vhidecur(void) -- hide the kernel hardware cursor (the game draws its own
; playfield; a blinking cursor in the middle of it looks like a bug).
.proc _vhidecur
        lda     #$80
        sta     VREG_CURSOR_HI
        rts
.endproc

; unsigned char keystate(void) -- bitmask of the control keys currently held.
; bit0 up, bit1 down, bit2 left, bit3 right, bit4 fire (space), bit5 button2
; (left shift). Non-destructive: poll it every frame for as long as the key is
; down. Reads 0 when the host has no GUI (console build, headless tests).
.proc _keystate
        lda     KEY_STATE
        ldx     #$00
        rts
.endproc

; int INCH_NB(void) -- non-blocking: next key 0..255, or -1 if none ready.
; The game polls this every pass of the loop so input stays responsive even
; when a simulation step is skipped.
.proc _INCH_NB
        jsr     K_GET_KEYSTROKE
        bcs     @got
        lda     #$ff
        ldx     #$ff            ; -1
        rts
@got:   ldx     #$00            ; 0..255
        rts
.endproc

; unsigned int rng_seed(void) -- latch the RTC and fold seconds/minutes/hours plus
; the packed FAT-time bits into a 16-bit seed (A=lo, X=hi). Varies per launch;
; the caller guards against a zero result.
.proc _rng_seed
        sta     RTC_LATCH       ; snapshot the live clock (written value irrelevant)
        lda     RTC_SEC
        eor     RTC_FATTIME_LO
        eor     #$E1            ; keep it lively
        pha                     ; -> low byte
        lda     RTC_MIN
        eor     RTC_HOUR
        eor     RTC_FATTIME_HI
        eor     #$AC
        tax                     ; -> high byte
        pla                     ; low byte back into A
        rts
.endproc

; unsigned char rtc_sec(void) -- latch the clock and return the BCD seconds byte.
; Only tested for change (a new second passed), so BCD is fine.
.proc _rtc_sec
        sta     RTC_LATCH
        lda     RTC_SEC
        ldx     #$00
        rts
.endproc

; void sound_tone(unsigned int freq) -- start a tone on voice 1. cc65 already
; passes a 16-bit argument in A=lo/X=hi, which is what K_SOUND_TONE wants, so this
; is a straight tail call. The kernel honours SOUND_ENABLE, so this is silent
; rather than wrong on a muted machine.
.proc _sound_tone
        jmp     K_SOUND_TONE
.endproc

; void sound_off(void) -- gate voice 1 off.
.proc _sound_off
        jmp     K_SOUND_OFF
.endproc

.proc _QUITDOS
        jsr     K_CLEAR_SCREEN  ; wipe the playfield + re-home the kernel cursor
        jmp     DOS_WARM        ; (also restores the full-screen scroll region)
.endproc
