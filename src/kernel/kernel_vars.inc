; ================================================================
; kernel_vars.inc - addresses shared by the kernel BIOS and the monitor
; ================================================================
; Split out of kernel.asm so the two halves cannot drift: every zero-page slot,
; page-2 variable, I/O register and hardware constant lives here exactly once.
; Included by kernel.asm; will also be included by the monitor once it becomes a
; bank module of its own, which is the point -- both sides must agree on where
; MON_CMDBUF, MON_CURRADDR and the rest live, because the DOS shell reaches into
; the same page-2 buffer via _DOS_ARGPTR.
;
; Addresses only. No code, so it can be included anywhere without emitting bytes.
; ================================================================
; ================================================================
; SYSTEM CONSTANTS SECTION
; ================================================================
STACK_TOP          = $FF           ; Top of stack page

; ================================================================
; ZERO PAGE VARIABLES - Relocated for BASIC compatibility
; ================================================================
; Original locations $00-$10, $F0-$FF conflicted with BASIC interpreter
; Relocated to $14-$3F range which is unused by BASIC
MON_CURRADDR_LO    = $14           ; Current address low byte (was $00)
MON_CURRADDR_HI    = $15           ; Current address high byte (was $01)
MON_MSG_PTR_LO     = $16           ; Message pointer low byte (was $02)
MON_MSG_PTR_HI     = $17           ; Message pointer high byte (was $03)
JUMP_VECTOR        = $18           ; Indirect jump vector (2 bytes: $18-$19) (was $04-$05)
; $1A-$1D: video cell-index scratch. The screen left the 64K map (it lives behind
; the VIC register port now), so the old 16-bit SCREEN_PTR is gone; these hold the
; computed cell index (CURSOR_Y*80 + CURSOR_X) and a temporary while computing it.
VID_CELL_LO        = $1A           ; computed cell index low
VID_CELL_HI        = $1B           ; computed cell index high
VID_TMP_LO         = $1C           ; cell-computation temporary low
VID_TMP_HI         = $1D           ; cell-computation temporary high
; $1E-$20: kernel pager flags (see PAGE_ADVANCE / GET_KEYSTROKE).
PAGE_ENABLE        = $1E           ; pager master switch (1=on; a future setting toggles it)
PAGE_SUSPEND       = $1F           ; ESC at --MORE-- suspends paging for the rest of this output
PAGE_IN_BREAK      = $20           ; guard: inside the page-break prompt (don't re-count)
CMD_LINE_COUNT     = $21           ; Lines printed since the last pause/command (was $0D)
PAGE_ABORT_FLAG    = $22           ; Set to 1 if user pressed ESC (was $0E)
RNG_STATE_LO       = $23           ; RNG 16-bit LFSR state, low byte (was RNG_SEED)
RNG_MAX            = $24           ; Maximum value for random number (was $10)
; $25-$34 was the runtime hex lookup table; now reclaimed (NIBBLE_TO_ASCII
; computes hex digits). $25-$28 reused as M: move scratch; $29-$32 since taken by
; sound/RNG/jiffy state; $33-$34 still free.
MOVE_DEST_LO       = $25           ; M: captured original destination low (copy mutates MON_DEST_ADDR)
MOVE_DEST_HI       = $26           ; M: captured original destination high
MOVE_DEND_LO       = $27           ; M: destination end = dest + (end - start), low
MOVE_DEND_HI       = $28           ; M: destination end high
SOUND_ENABLE       = $29           ; 1 = system sound on (BEL beep + sound ABI); 0 = muted
BEEP_TIMER         = $2A           ; jiffies until the BEL beep auto-gates-off (0 = idle)
RNG_STATE_HI       = $2B           ; RNG 16-bit LFSR state, high byte
RNG_TMP            = $2F           ; RNG scratch: multiplier during range reduction
RNG_TMP2           = $30           ; RNG scratch: product low during range reduction
JIFFY_LO           = $31           ; 60 Hz monotonic tick counter, low byte (K_GET_JIFFIES)
JIFFY_HI           = $32           ; 60 Hz monotonic tick counter, high byte
DEC_TEMP_LO        = $35           ; Decimal conversion temporary low byte
DEC_TEMP_HI        = $36           ; Decimal conversion temporary high byte
DEC_DIGIT_IDX      = $37           ; Decimal digit index/counter
DEC_RESULT_LO      = $38           ; Decimal conversion result low byte
DEC_RESULT_HI      = $39           ; Decimal conversion result high byte
; PRINT_DEC (32-bit decimal) reuses the same $35-$39 workspace (it never runs
; concurrently with the H:/D: parse/print): $35-$38 = value, $39 = digit count.
DEC32_VAL          = $35           ; 4-byte little-endian value ($35-$38)
DEC32_CNT          = $39           ; digit count

; ================================================================
; MONITOR VARIABLES
; ================================================================

; Monitor Command Buffer and State Variables
MON_CMDBUF         = $0200         ; Command input buffer (80 bytes: $0200-$024F)
MON_CMDBUF_LEN     = 80            ; Maximum command buffer length

; Monitor variables relocated to $0269+ to avoid BASIC memory conflicts
; BASIC uses $0200-$0268, Monitor command buffer $0200-$024F overlaps but is inactive during BASIC mode
MON_CMDLEN         = $026A         ; Current length of command in buffer (was $0251)
MON_MODE           = $026B         ; Current monitor mode (0=Command, 1=Write) (was $0252)
MON_STARTADDR_LO   = $026C         ; Start address for range operations (low) (was $0253)
MON_STARTADDR_HI   = $026D         ; Start address for range operations (high) (was $0254)
MON_ENDADDR_LO     = $026E         ; End address for range operations (low) (was $0255)
MON_ENDADDR_HI     = $026F         ; End address for range operations (high) (was $0256)
MON_PARSE_PTR      = $0270         ; Parser position pointer (was $0257)
MON_HEX_TEMP       = $0272         ; Temporary storage for hex conversion (was $0259)
MON_BYTE_COUNT     = $0273         ; Count of bytes for write operations (was $025A)
MON_ERROR_FLAG     = $0275         ; Error flag for invalid operations (was $025C)
CURSOR_X           = $0276         ; Current cursor X position (0-39) (was $025D)
CURSOR_Y           = $0277         ; Current cursor Y position (0-24) (was $025E)
MON_FILL_VALUE     = $0279         ; Fill command byte value (was $0260)
MON_DEST_ADDR_LO   = $027A         ; Move/Copy destination address low byte (was $0261)
MON_DEST_ADDR_HI   = $027B         ; Move/Copy destination address high byte (was $0262)
MON_COPY_MODE      = $027C         ; Move/Copy mode (0=copy, 1=move) (was $0263)
MON_DUMP_SNAP      = $02DF         ; non-zero: DUMP_MEMORY_RANGE reads MON_SNAP_BUF
MON_SNAP_BUF       = $0400         ; 256-byte page snapshot for T:/Z: (free RAM)
; $0500-$07FF is the assembler's workspace (identifier buffers + symbol table; see
; assembler.inc). It lives here rather than in user RAM because this page -- the old
; 40x25 screen, unused since the display moved behind the VIC port -- is below
; Ram_base and so costs user programs nothing. It cannot live in the module bank:
; that window is ROM while a module is mapped, and a symbol table must be written.
MON_SEARCH_PATTERN = $027D         ; Search pattern buffer (16 bytes: $027D-$028C) (was $0264-$0273)
MON_PATTERN_LEN    = $028D         ; Search pattern length (1-16 bytes) (was $0274)
MON_LAST_CMD_BUF   = $028E         ; Last command buffer (80 bytes: $028E-$02DD) (was $0275-$02C4)
MON_LAST_CMD_LEN   = $02DE         ; Last command length (1 byte) (was $02C5)

; Monitor Mode Constants
MON_MODE_CMD       = 0             ; Command mode
MON_MODE_WRITE     = 1             ; Write mode

; Monitor Display Constants
MON_BYTES_PER_LINE = 16            ; Bytes per dump line (16 fits the 80-col screen)

; Screen and I/O Constants for Monitor. The screen lives behind the VIC register
; port (not in the 64K map); the kernel tracks the logical cursor in CURSOR_X/Y
; and writes through VREG_* (see SCREEN OUTPUT routines below).
SCREEN_WIDTH       = 80            ; Characters per line
SCREEN_HEIGHT      = 25            ; Lines on screen
LINES_PER_PAGE     = 24            ; Full screen height

; ASCII Character Constants
ASCII_CR           = $0D           ; Carriage return
ASCII_LF           = $0A           ; Line feed
ASCII_SPACE        = $20           ; Space
ASCII_COLON        = $3A           ; Colon ':'
ASCII_COMMA        = $2C           ; Comma ',' (field separator)
ASCII_DASH         = $2D           ; Dash '-'
ASCII_BACKSPACE    = $08           ; Backspace character
ASCII_BEL          = $07           ; Bell (rings a short beep via the SID)
ASCII_DELETE       = $7F           ; Delete character
ASCII_ESC          = $1B           ; Escape character
ASCII_DOT          = $2E           ; Dot '.' character
; ----------------------------------------------------------------
; MFC-DOS ABI - fixed entry points in the always-mapped DOS ROM ($8800-$AFFF).
; The kernel reaches the resident DOS shell here (the DOS ROM is always mapped).
; See src/kernel/dos/dos.asm. The DOS owns the filesystem and file commands; the
; monitor is a pure debugger launched by the DOS 'MON' command.
; ----------------------------------------------------------------
DOS_COLD           = $AF00         ; DOS shell cold entry (boot target)
DOS_WARM           = $AF1E         ; DOS shell re-entry (monitor Q returns here)

; Hardware I/O addresses for keyboard input
PIA_DATA           = $FE00         ; PIA data register for keyboard
PIA_CONTROL        = $FE02         ; PIA control register for keyboard
PIA_DATA_AVAIL     = $01           ; Bit mask for data available flag

; File I/O interface addresses
FILE_COMMAND       = $FE10         ; File operation command
FILE_STATUS        = $FE11         ; File operation status
FILE_ADDR_LO       = $FE12         ; Target address low byte
FILE_ADDR_HI       = $FE13         ; Target address high byte
FILE_NAME_BUF      = $FE14         ; Filename buffer start ($FE14-$FE1F)

; Additional file I/O registers for save operations
FILE_END_ADDR_LO   = $FE20         ; End address low byte for save range
FILE_END_ADDR_HI   = $FE21         ; End address high byte for save range

; Module-slot bank select. Writing n maps bank n into the $B000-$DFFF module
; window: 0 = RAM (boot/default), 1..255 = read-only ROM modules. Reading returns
; the current bank. Lives in the always-mapped I/O page (see docs/ARCHITECTURE.md, Part 4).
MODULE_BANK        = $FE23         ; bank-select register for the $B000-$DFFF window
MODULE_BANK_RAM    = $00           ; bank value that maps the window to RAM
MODULE_WINDOW_START = $B000        ; base of the bankable module window
MODULE_WINDOW_END  = $EFFF         ; last byte of the bankable module window (16 KB)

; Monitor module (bank 4). The monitor lives in the bankable window rather than in
; kernel ROM: as a disk program it would load at $0800 and so overwrite the very
; code it exists to debug, while a bank costs no user RAM and is present even with
; no disk. The accepted blind spot is that it cannot inspect its own window
; ($B000-$DFFF) or a sibling bank -- everything else in the map is visible.
MON_BANK           = $04           ; MODULE_BANK value that maps the monitor
MON_ENTRY_COLD     = $B000         ; cold entry: banner + fresh prompt (DOS 'MON')
MON_ENTRY_BREAK    = $B003         ; break-in entry: fresh prompt, no banner (NMI STOP)

; VDC-style video register port ($FE2D-$FE36). The 80x25 character and color
; planes live inside the VIC, NOT in the 64K map. Set a cell index via
; VREG_ADDR_LO/HI, then read/write the auto-incrementing VREG_CHAR/VREG_COLOR
; data ports. VREG_ATTR is the current-attribute latch applied to VREG_CHAR
; writes; VREG_CMD drives chip-side block ops; VREG_CURSOR_LO/HI position the
; displayed cursor (high-byte bit7 = hidden). See docs/ARCHITECTURE.md, Part 2.
VREG_ADDR_LO       = $FE2D         ; cell index low (0..1999)
VREG_ADDR_HI       = $FE2E         ; cell index high
VREG_CHAR          = $FE2F         ; char data port (auto-increments index)
VREG_COLOR         = $FE30         ; color data port (auto-increments index)
VREG_ATTR          = $FE31         ; current attribute latch
VREG_CMD           = $FE32         ; command engine
VREG_STATUS        = $FE33         ; 0 = ready
VREG_CURSOR_LO     = $FE34         ; cursor cell low
VREG_CURSOR_HI     = $FE35         ; cursor cell high (bit7 = hidden)
VREG_CMD_PARAM     = $FE36         ; command parameter / fill char

; RTC (real-time clock) registers in the I/O page. Writing RTC_LATCH snapshots the
; live clock into the read registers. Used here only to seed the RNG at boot so
; random sequences differ from run to run.
RTC_LATCH          = $FE55
RTC_SEC            = $FE56         ; BCD seconds
RTC_MIN            = $FE57         ; BCD minutes
RTC_HOUR           = $FE58         ; BCD hours
RTC_FATTIME_LO     = $FE5D         ; host-packed FAT time (sub-second-ish entropy)
RTC_FATTIME_HI     = $FE5E

; Soft power switch ($FE61). Two writes in order cut the power: POWER_ARM then
; POWER_FIRE, and any other value cancels a half-entered sequence. A single magic
; byte would let one wild pointer take the machine down, which is why real
; soft-power and watchdog registers wanted an unlock sequence too.
;
; Distinct from the CPU's STP, deliberately. STP stops the processor and its clock
; -- it exists for battery work, where the part sleeps until a RESET wakes it -- so
; it means "the processor is off" while the system around it still has power.
; Switching the machine off is a system-level act and gets a system-level register.
POWER_REG          = $FE61
POWER_ARM          = $5A
POWER_FIRE         = $A5

VCMD_CLEAR         = $01           ; fill whole screen (char=param, color=latch)
VCMD_SCROLL_UP     = $02           ; scroll up one row, blank bottom
VCMD_SCROLL_DOWN   = $03           ; scroll down one row, blank top
VCMD_FILL_ROW      = $04           ; fill the row of the current cell

; File command codes
FILE_LOAD_CMD      = $01           ; Load file command
FILE_SAVE_CMD      = $02           ; Save file command

; File status codes
FILE_IN_PROGRESS   = $01           ; Operation in progress
FILE_SUCCESS       = $02           ; Operation completed successfully

; Live control-key state ($FE0F, read-only): which of the game keys are held
; RIGHT NOW, as a bitmask -- b0 up, b1 down, b2 left, b3 right, b4 fire (space),
; b5 button2 (left shift), b6-7 reserved. Distinct from the keystroke buffer at
; KEYBOARD_DATA, which records what was typed and carries no key-up. Nothing in
; the kernel uses this; it is declared here so the address is allocated in one
; place. Action games poll it directly (see programs/kpanic/glue.s).
KEY_STATE          = $FE0F

; Interrupt-related addresses and bits
TIMER_IRQ_ACK      = $FE0E         ; write to acknowledge the periodic timer IRQ
BASIC_NMI_FLAGS    = $DC           ; EhBASIC NmiBase (zero page): b7=enabled, b5=happened
BASIC_IRQ_FLAGS    = $DF           ; EhBASIC IrqBase (zero page): b7=enabled, b5=happened
INT_ENABLED        = $80           ; "interrupt enabled" bit in the above
INT_HAPPENED       = $20           ; "interrupt happened" bit (set by the ISR for BASIC)

; SID sound chip ($FE38-$FE54; real 6581/8580 register layout). Voice 1 is used
; for the system beep and the K_SOUND_TONE/K_SOUND_OFF ABI.
SID_V1_FREQLO      = $FE38         ; voice 1 frequency low
SID_V1_FREQHI      = $FE39         ; voice 1 frequency high
SID_V1_CTRL        = $FE3C         ; voice 1 control: b0 gate, b4 triangle
SID_V1_AD          = $FE3D         ; voice 1 attack (hi) / decay (lo)
SID_V1_SR          = $FE3E         ; voice 1 sustain (hi) / release (lo)
SID_MODEVOL        = $FE50         ; filter mode (hi nibble) + master volume (lo)
SID_CTRL_TONE      = $11           ; triangle ($10) + gate ($01): a clean beep tone
BEEP_FREQ_LO       = $AC           ; $39AC ~= 880 Hz at the nominal 1 MHz SID clock
BEEP_FREQ_HI       = $39
BEEP_LEN_JIFFIES   = $08           ; beep duration in ~60 Hz timer ticks (~130 ms)

