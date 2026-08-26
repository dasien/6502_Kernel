; ============================================================================
; KERNEL PANIC -- cc65 runtime glue
; ============================================================================
; The 80x25 screen lives behind the VIC register port (not in the 64K map), so
; all drawing goes through the video helpers: set the cell index with vaddr(),
; then stream glyphs with vputc() (the port auto-increments).
;
; Game-specific additions over the VAULT glue:
;   jiffies()    -- the kernel's 60 Hz monotonic tick counter (K_GET_JIFFIES,
;                   kernel v3.23). This is what paces the fixed-tick loop.
;   vscrollbot() -- set the scroll-region bottom row, so chip-side scrolls move
;                   the playfield and leave the HUD rows below it pinned.
; ============================================================================

.export _INCH_NB, _QUITDOS, _keystate
.export _vaddr, _vputc, _vattr
.export _vfill, _vcmd, _vscrollbot, _vhidecur
.export _spr_sel, _spr_x, _spr_x_px, _spr_y, _spr_y_px, _spr_glyph, _spr_attr, _spr_on
.export _rng_seed, _rtc_sec, _jiffies
.export _vfseek, _vfread, _vfwrite

K_GET_KEYSTROKE = $FF09         ; non-blocking: C set + A=char
K_CLEAR_SCREEN  = $FF0C         ; clear + home (also resets the kernel cursor)
K_GET_JIFFIES   = $FF39         ; 60 Hz monotonic counter -> A=lo, X=hi
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
VREG_SCROLL_BOT = $FE37         ; scroll-region bottom row (scroll/fill hit rows 0..this)

; Soft font: the glyph shapes are RAM, not a fixed ROM. A 16-bit index selects a byte
; across every font set (set N starts at N*4096), and the data port auto-increments, so
; a glyph is one seek plus 16 sequential accesses. Readable as well as writable, which is
; what lets a program lift a CP437 shape out and build on top of it instead of shipping
; hand-drawn artwork for something the font already has.
VREG_FONT_LO    = $FE62         ; font byte index low
VREG_FONT_HI    = $FE63         ; font byte index high
VREG_FONT_DATA  = $FE64         ; font data, auto-increments

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

; void vfseek(unsigned int index) -- point the font data port at a byte (A=lo, X=hi).
.proc _vfseek
        sta     VREG_FONT_LO
        stx     VREG_FONT_HI
        rts
.endproc

; unsigned char vfread(void) -- read a font byte; the port then advances.
.proc _vfread
        lda     VREG_FONT_DATA
        ldx     #$00
        rts
.endproc

; void vfwrite(unsigned char b) -- write a font byte; the port then advances.
.proc _vfwrite
        sta     VREG_FONT_DATA
        rts
.endproc

; void vscrollbot(unsigned char row) -- bound the scroll region to rows 0..row.
; NOTE: a clear command resets this to the full screen, so always set it AFTER
; clearing, never before.
; ---- sprites ------------------------------------------------------------
; A sprite is pixel-positioned and does NOT ride the scroll region, which is the whole
; reason these exist: anything in the cell plane rides the fine-scroll offset, so a
; screen-fixed object sawtooths by a cell on every scroll.
;
; Select-then-set, so every setter takes a SINGLE argument and therefore arrives in A
; with no C-stack handling at all. spr_sel() converts a sprite index into a byte offset
; once; the setters then index the register block with it. Positions are given in CELLS
; and converted here -- the chip wants nominal pixels on the 8x16 grid.
SPR0            = $FE65
SPR_STRIDE      = 6
SPR_X_LO        = SPR0+0
SPR_X_HI        = SPR0+1        ; bits 1-0
SPR_Y_LO        = SPR0+2
SPR_Y_HI        = SPR0+3        ; bits 1-0 = y high, bit 7 = enable
SPR_GLYPH       = SPR0+4
SPR_ATTR        = SPR0+5

; void spr_sel(unsigned char index) -- offset = index * 6, so index*4 + index*2.
; 24 sprites max here, so the product always fits a byte.
.proc _spr_sel
        asl     a               ; index*2
        sta     spr_tmp
        asl     a               ; index*4
        clc
        adc     spr_tmp         ; index*6
        sta     spr_off
        rts
.endproc

; void spr_x(unsigned char cell_col) -- x = col * 8, ten bits wide
.proc _spr_x
        ldx     spr_off
        pha
        lda     #$00
        sta     spr_tmp         ; high bits accumulate here
        pla
        asl     a
        rol     spr_tmp
        asl     a
        rol     spr_tmp
        asl     a
        rol     spr_tmp
        sta     SPR_X_LO,x
        lda     spr_tmp
        and     #$03
        sta     spr_tmp
        ; Read-modify-write: bits 4-2 of this byte are the sprite's WIDTH and bit 5 is
        ; its X magnify. Storing the position alone would zero them. They read as 1x1
        ; unmagnified when zero, so KPANIC happens not to care -- but clobbering another
        ; program's size on a position write is the kind of thing that is very hard to
        ; find later.
        lda     SPR_X_HI,x
        and     #$FC
        ora     spr_tmp
        sta     SPR_X_HI,x
        rts
.endproc

; void spr_x_px(unsigned int px) -- X straight in nominal pixels, for an object that
; sits BETWEEN columns. cc65 passes a lone unsigned int in A/X, so Y is the register
; index here because X carries the argument's high byte. Same read-modify-write on the
; high byte as _spr_x: bits 4-2 are WIDTH and bit 5 is X magnify.
.proc _spr_x_px
        ldy     spr_off
        sta     SPR_X_LO,y
        txa
        and     #$03
        sta     spr_tmp
        lda     SPR_X_HI,y
        and     #$FC            ; keep WIDTH and X magnify
        ora     spr_tmp
        sta     SPR_X_HI,y
        rts
.endproc

; void spr_y(unsigned char cell_row) -- y = row * 16, nine bits wide. Preserves the
; enable bit so a caller can reposition without re-enabling.
.proc _spr_y
        ldx     spr_off
        pha
        lda     #$00
        sta     spr_tmp
        pla
        asl     a
        rol     spr_tmp
        asl     a
        rol     spr_tmp
        asl     a
        rol     spr_tmp
        asl     a
        rol     spr_tmp
        sta     SPR_Y_LO,x
        lda     spr_tmp
        and     #$03
        sta     spr_tmp
        lda     SPR_Y_HI,x
        and     #$FC            ; keep enable, Y magnify and HEIGHT
        ora     spr_tmp
        sta     SPR_Y_HI,x
        rts
.endproc

; void spr_y_px(unsigned int py) -- Y straight in nominal pixels, for an object that
; sits BETWEEN rows. cc65 passes a lone unsigned int in A/X. Uses Y as the register
; index because X carries the argument's high byte.
.proc _spr_y_px
        ldy     spr_off
        sta     SPR_Y_LO,y
        txa
        and     #$03
        sta     spr_tmp
        lda     SPR_Y_HI,y
        and     #$FC            ; keep enable, Y magnify and HEIGHT
        ora     spr_tmp
        sta     SPR_Y_HI,y
        rts
.endproc

.proc _spr_glyph
        ldx     spr_off
        sta     SPR_GLYPH,x
        rts
.endproc

.proc _spr_attr
        ldx     spr_off
        sta     SPR_ATTR,x
        rts
.endproc

; void spr_on(unsigned char enable) -- leaves the position alone
.proc _spr_on
        ldx     spr_off
        cmp     #$00
        beq     @off
        lda     SPR_Y_HI,x
        ora     #$80
        sta     SPR_Y_HI,x
        rts
@off:   lda     SPR_Y_HI,x
        and     #$7F
        sta     SPR_Y_HI,x
        rts
.endproc

.segment "BSS"
spr_off:        .res 1          ; selected sprite's byte offset into the block
spr_tmp:        .res 1
.segment "CODE"

.proc _vscrollbot
        sta     VREG_SCROLL_BOT
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

.proc _QUITDOS
        jsr     K_CLEAR_SCREEN  ; wipe the playfield + re-home the kernel cursor
        jmp     DOS_WARM        ; (also restores the full-screen scroll region)
.endproc
