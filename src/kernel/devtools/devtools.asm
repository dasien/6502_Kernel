; ================================================================
; MFC 6502 DEV TOOLS MODULE  (assembler / disassembler)
; ================================================================
; Filename:     devtools.asm
; Author:       Brian Gentry
; Version:      0.1 (Phase 4 sub-step 2: module skeleton)
; Assembler:    ca65  (-I src/kernel/devtools for opcodes_65c02.inc)
;
; A bank-switched ROM module for the $B000-$DFFF module window (module bank 2;
; see docs/module_slot_design.md). The kernel's B: bank menu maps this bank and
; JMPs to the entry at $B000; the module returns to the monitor via $FF12.
;
; Module ABI: the module reaches the system ONLY through the kernel jump table
; at $FF00 (the kernel ROM is always mapped, regardless of the active bank). It
; uses $0800-$AFFF as working RAM and may use zero page freely while it runs
; (the monitor is suspended; the return handler resets the stack).
;
; This skeleton (sub-step 2) just proves the module path end-to-end: it shows a
; banner and waits for ESC to return. The disassembler and assembler land in the
; following sub-steps and will use the opcode table included below.
; ================================================================

.PC02                               ; enable 65C02 instructions (BRA, etc.)

; ---- Kernel API jump table (the module ABI) --------------------
K_PRINT_CHAR     = $FF00            ; print A as a character
K_PRINT_MESSAGE  = $FF03            ; print null-terminated string at ($16)
K_PRINT_NEWLINE  = $FF06            ; print CR/LF
K_GET_KEYSTROKE  = $FF09            ; A=key, carry set if one was available
K_CLEAR_SCREEN   = $FF0C            ; clear the screen, home the cursor
K_RETURN_MODULE  = $FF12            ; exit: unmap this bank, return to monitor

ASCII_ESC        = $1B

; ---- Module zero-page scratch ----------------------------------
; $FB-$FC sit outside the monitor's ZP ($14-$39); the monitor doesn't rely on
; them across the call, so they are safe transient scratch for the module.
DEVT_STR_PTR     = $FB              ; string pointer for PRINT_STR

.segment "CODE"

; ----------------------------------------------------------------
; DEVT_MAIN - module entry point (MUST be first byte of CODE = $B000)
; ----------------------------------------------------------------
DEVT_MAIN:
    JSR K_CLEAR_SCREEN

    LDA #<MSG_BANNER
    LDY #>MSG_BANNER
    JSR PRINT_STR

    LDA #<MSG_PROMPT
    LDY #>MSG_PROMPT
    JSR PRINT_STR

DEVT_LOOP:
    JSR K_GET_KEYSTROKE
    BCC DEVT_LOOP                   ; no key yet
    CMP #ASCII_ESC
    BEQ DEVT_EXIT
    BRA DEVT_LOOP                   ; ignore everything else (for now)

DEVT_EXIT:
    JMP K_RETURN_MODULE             ; unmaps bank 2, returns to the monitor

; ----------------------------------------------------------------
; PRINT_STR - print a null-terminated string via the kernel
; Input: A = string pointer low, Y = string pointer high
; Modifies: A, Y (K_PRINT_CHAR preserves Y across the call)
; ----------------------------------------------------------------
PRINT_STR:
    STA DEVT_STR_PTR
    STY DEVT_STR_PTR+1
    LDY #$00
PRINT_STR_LOOP:
    LDA (DEVT_STR_PTR),Y
    BEQ PRINT_STR_DONE
    JSR K_PRINT_CHAR
    INY
    BNE PRINT_STR_LOOP              ; strings are < 256 bytes
PRINT_STR_DONE:
    RTS

MSG_BANNER:
    .byte "MFC DEV TOOLS v0.1", $0D, $0A
    .byte "ASSEMBLER / DISASSEMBLER", $0D, $0A, $0A, $00
MSG_PROMPT:
    .byte "PRESS ESC TO RETURN TO MONITOR", $0D, $0A, $00

; ----------------------------------------------------------------
; Canonical 65C02 opcode/addressing-mode table (generated from CPU6502).
; Unused by this skeleton; wired in for the disassembler/assembler sub-steps,
; and so the build's -I include path is exercised now.
; ----------------------------------------------------------------
.segment "DATA"
.include "opcodes_65c02.inc"
