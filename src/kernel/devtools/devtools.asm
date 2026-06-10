; ================================================================
; MFC 6502 DEV TOOLS MODULE  (assembler / disassembler)
; ================================================================
; Filename:     devtools.asm
; Author:       Brian Gentry
; Version:      0.3 (Phase 4 sub-step 3: disassembler, on the extended ABI)
; Assembler:    ca65  (-I src/kernel/devtools for opcodes_65c02.inc)
;
; A bank-switched ROM module for the $B000-$DFFF window (module bank 2; see
; docs/module_slot_design.md). The kernel's B: bank menu maps this bank and JMPs
; to the entry at $B000; the module returns to the monitor via $FF12.
;
; Module ABI: reaches the system ONLY through the kernel jump table at $FF00 (the
; kernel ROM is always mapped regardless of the active bank). It reuses the
; kernel's line input, hex parsing, and hex printing rather than duplicating
; them; per the ABI those share the monitor's command buffer (MON_CMDBUF) and
; MON_CURRADDR as scratch (the monitor is suspended and that state is saved and
; restored across the launch). Working RAM is $0800-$AFFF.
;
; Commands (ENTER to run; ESC on an empty line returns to the monitor):
;   D xxxx   disassemble from hex address xxxx (one screen of instructions)
; ================================================================

.PC02                               ; enable 65C02 instructions (BRA, STZ, ...)

; ---- Kernel API jump table (the module ABI) --------------------
K_PRINT_CHAR     = $FF00            ; print A as a character (preserves X, Y)
K_PRINT_MESSAGE  = $FF03            ; print null-terminated string at (MON_MSG_PTR)
K_PRINT_NEWLINE  = $FF06            ; print CR/LF
K_GET_KEYSTROKE  = $FF09            ; A=key, carry set if one was available
K_CLEAR_SCREEN   = $FF0C            ; clear the screen, home the cursor
K_RETURN_MODULE  = $FF12            ; exit: unmap this bank, return to monitor
K_READ_LINE      = $FF15            ; edited line input -> MON_CMDBUF / MON_CMDLEN
K_PARSE_HEX      = $FF18            ; X=offset in MON_CMDBUF -> MON_CURRADDR, C=invalid
K_PRINT_HEX_BYTE = $FF1B            ; print A as two hex digits

; ---- Shared kernel state used as module scratch (per the ABI) --
MON_CURRADDR_LO  = $14
MON_CURRADDR_HI  = $15
MON_MSG_PTR_LO   = $16
MON_MSG_PTR_HI   = $17
MON_CMDBUF       = $0200            ; command input buffer (filled by K_READ_LINE)
MON_CMDLEN       = $026A            ; length of the line in MON_CMDBUF

ASCII_ESC        = $1B
DISASM_COUNT     = 16               ; instructions disassembled per D command

; ---- Operand "kind" codes (how PRINT_OPERAND renders a mode) ---
KIND_NONE        = 0                ; no operand (implied, undefined/NOP)
KIND_ACC         = 1                ; "A"
KIND_BYTE        = 2                ; prefix + 1 operand byte + suffix
KIND_WORD        = 3                ; prefix + 2 operand bytes + suffix
KIND_REL         = 4                ; "$" + computed branch target
KIND_ZPREL       = 5                ; "$zp,$target" (BBRn/BBSn)

; ---- Module zero-page scratch ($F0-$FE; outside the monitor's $14-$39) -----
DA_OPCODE        = $F0
DA_MODE          = $F1
DA_LEN           = $F2
DA_MNEM          = $F3
DA_TMP           = $F4              ; kind / relative byte
DA_TGT           = $F5              ; branch target / multiply temp ($F5-$F6)
MNE_PTR          = $FB              ; mnemonic string pointer ($FB-$FC)
DA_ADDR          = $FD              ; current disassembly address ($FD-$FE)

.segment "CODE"

; ----------------------------------------------------------------
; DEVT_MAIN - module entry point (MUST be first byte of CODE = $B000)
; ----------------------------------------------------------------
DEVT_MAIN:
    JSR K_CLEAR_SCREEN
    LDA #<MSG_BANNER
    LDY #>MSG_BANNER
    JSR PUTS
    LDA #<MSG_PROMPT
    LDY #>MSG_PROMPT
    JSR PUTS

; ----------------------------------------------------------------
; Command loop: prompt, read a line (kernel handles editing/backspace/ESC),
; dispatch on the first character.
; ----------------------------------------------------------------
DEVT_LOOP:
    LDA #'>'
    JSR K_PRINT_CHAR
    JSR K_READ_LINE                 ; -> MON_CMDBUF, MON_CMDLEN
    LDA MON_CMDLEN
    BEQ DEVT_LOOP                   ; empty line -> reprompt
    LDA MON_CMDBUF
    CMP #ASCII_ESC                  ; bare ESC (length 1) -> exit
    BEQ DEVT_EXIT
    CMP #'D'
    BEQ CMD_DISASM
    BRA DEVT_ERROR                  ; unknown command
DEVT_EXIT:
    JMP K_RETURN_MODULE             ; unmaps bank 2, returns to the monitor
DEVT_ERROR:
    LDA #'?'
    JSR K_PRINT_CHAR
    JSR K_PRINT_NEWLINE
    BRA DEVT_LOOP

; ----------------------------------------------------------------
; CMD_DISASM - "D xxxx": disassemble DISASM_COUNT instructions from xxxx
; ----------------------------------------------------------------
CMD_DISASM:
    LDX #$01                        ; skip the command letter
CD_SKIP:
    CPX MON_CMDLEN
    BCS DEVT_ERROR                  ; nothing but spaces after D
    LDA MON_CMDBUF,X
    CMP #' '
    BNE CD_PARSE
    INX
    BRA CD_SKIP
CD_PARSE:
    JSR K_PARSE_HEX                 ; X=offset -> MON_CURRADDR; carry set if invalid
    BCS DEVT_ERROR
    LDA MON_CURRADDR_LO
    STA DA_ADDR
    LDA MON_CURRADDR_HI
    STA DA_ADDR+1
    LDX #DISASM_COUNT
CD_LOOP:
    PHX                             ; DISASM_ONE uses X
    JSR DISASM_ONE
    PLX
    DEX
    BNE CD_LOOP
    BRA DEVT_LOOP

; ----------------------------------------------------------------
; DISASM_ONE - decode and print one instruction at (DA_ADDR), advance DA_ADDR
; Output line: "AAAA: BB BB BB  MNE operand"
; ----------------------------------------------------------------
DISASM_ONE:
    LDA DA_ADDR+1
    JSR K_PRINT_HEX_BYTE
    LDA DA_ADDR
    JSR K_PRINT_HEX_BYTE
    LDA #':'
    JSR K_PRINT_CHAR
    LDA #' '
    JSR K_PRINT_CHAR

    ; decode opcode -> mode, mnemonic id, length
    LDY #$00
    LDA (DA_ADDR),Y
    STA DA_OPCODE
    TAX
    LDA OPC_MODE,X
    STA DA_MODE
    LDA OPC_MNEM,X
    STA DA_MNEM
    LDX DA_MODE
    LDA MODE_LEN,X
    STA DA_LEN

    ; raw bytes, padded to a fixed 3-byte (9-char) column
    LDY #$00
DA_RAW:
    CPY DA_LEN
    BCS DA_RAW_PAD
    LDA (DA_ADDR),Y
    JSR K_PRINT_HEX_BYTE
    LDA #' '
    JSR K_PRINT_CHAR
    INY
    BRA DA_RAW
DA_RAW_PAD:
    CPY #3
    BCS DA_RAW_DONE
    LDA #' '
    JSR K_PRINT_CHAR
    LDA #' '
    JSR K_PRINT_CHAR
    LDA #' '
    JSR K_PRINT_CHAR
    INY
    BRA DA_RAW_PAD
DA_RAW_DONE:
    LDA #' '
    JSR K_PRINT_CHAR

    ; mnemonic: pointer = MNEM_STR + DA_MNEM * MNEM_WIDTH
    LDA DA_MNEM
    STA DA_TGT
    LDA #$00
    STA DA_TGT+1
    ASL DA_TGT
    ROL DA_TGT+1
    ASL DA_TGT
    ROL DA_TGT+1                    ; DA_TGT = DA_MNEM * 4 (MNEM_WIDTH)
    LDA DA_TGT
    CLC
    ADC #<MNEM_STR
    STA MNE_PTR
    LDA DA_TGT+1
    ADC #>MNEM_STR
    STA MNE_PTR+1
    LDY #$00
DA_MNE:
    LDA (MNE_PTR),Y
    CMP #' '                        ; mnemonics are space-padded on the right
    BEQ DA_MNE_DONE
    JSR K_PRINT_CHAR
    INY
    CPY #MNEM_WIDTH
    BCC DA_MNE
DA_MNE_DONE:
    LDA #' '
    JSR K_PRINT_CHAR

    JSR PRINT_OPERAND
    JSR K_PRINT_NEWLINE

    ; advance DA_ADDR by the instruction length
    LDA DA_ADDR
    CLC
    ADC DA_LEN
    STA DA_ADDR
    BCC DA_ADV_DONE
    INC DA_ADDR+1
DA_ADV_DONE:
    RTS

; ----------------------------------------------------------------
; PRINT_OPERAND - render the operand for DA_MODE (operand bytes at DA_ADDR+1..)
; ----------------------------------------------------------------
PRINT_OPERAND:
    LDX DA_MODE
    LDA OPK,X
    BEQ PO_DONE                     ; KIND_NONE
    STA DA_TMP                      ; remember the kind
    LDA OPPRE_L,X                   ; print the prefix string for this mode
    STA MON_MSG_PTR_LO
    LDA OPPRE_H,X
    STA MON_MSG_PTR_HI
    JSR K_PRINT_MESSAGE
    LDA DA_TMP
    CMP #KIND_ACC
    BEQ PO_SUFFIX                   ; prefix already printed "A"
    CMP #KIND_BYTE
    BEQ PO_BYTE
    CMP #KIND_WORD
    BEQ PO_WORD
    CMP #KIND_REL
    BEQ PO_REL
    ; KIND_ZPREL (BBRn/BBSn): "$zp,$target"
    LDY #$01
    LDA (DA_ADDR),Y
    JSR K_PRINT_HEX_BYTE
    LDA #','
    JSR K_PRINT_CHAR
    LDA #'$'
    JSR K_PRINT_CHAR
    LDY #$02
    LDA (DA_ADDR),Y
    JSR COMPUTE_TARGET
    LDA DA_TGT+1
    JSR K_PRINT_HEX_BYTE
    LDA DA_TGT
    JMP K_PRINT_HEX_BYTE            ; tail call
PO_BYTE:
    LDY #$01
    LDA (DA_ADDR),Y
    JSR K_PRINT_HEX_BYTE
    BRA PO_SUFFIX
PO_WORD:
    LDY #$02
    LDA (DA_ADDR),Y
    JSR K_PRINT_HEX_BYTE           ; high byte first
    LDY #$01
    LDA (DA_ADDR),Y
    JSR K_PRINT_HEX_BYTE
    BRA PO_SUFFIX
PO_REL:
    LDY #$01
    LDA (DA_ADDR),Y
    JSR COMPUTE_TARGET
    LDA DA_TGT+1
    JSR K_PRINT_HEX_BYTE
    LDA DA_TGT
    JSR K_PRINT_HEX_BYTE
    ; fall through to suffix (empty for REL)
PO_SUFFIX:
    LDX DA_MODE
    LDA OPSUF_L,X
    STA MON_MSG_PTR_LO
    LDA OPSUF_H,X
    STA MON_MSG_PTR_HI
    JMP K_PRINT_MESSAGE             ; tail call
PO_DONE:
    RTS

; ----------------------------------------------------------------
; COMPUTE_TARGET - branch target into DA_TGT
; Input: A = relative byte; DA_ADDR and DA_LEN set.
;        target = DA_ADDR + DA_LEN + sign-extend(rel)
; ----------------------------------------------------------------
COMPUTE_TARGET:
    STA DA_TMP                      ; rel
    LDA DA_ADDR
    CLC
    ADC DA_LEN
    STA DA_TGT
    LDA DA_ADDR+1
    ADC #$00
    STA DA_TGT+1                    ; address of the next instruction
    LDA DA_TMP
    CLC
    ADC DA_TGT
    STA DA_TGT
    LDA DA_TGT+1
    ADC #$00
    STA DA_TGT+1
    LDA DA_TMP
    BPL CT_DONE
    DEC DA_TGT+1                    ; rel negative -> sign extension
CT_DONE:
    RTS

; ----------------------------------------------------------------
; PUTS - print a null-terminated string via the kernel (A=ptr lo, Y=ptr hi)
; ----------------------------------------------------------------
PUTS:
    STA MON_MSG_PTR_LO
    STY MON_MSG_PTR_HI
    JMP K_PRINT_MESSAGE             ; tail call

; ================================================================
; DATA
; ================================================================
.segment "DATA"

MSG_BANNER:
    .byte "MFC DEV TOOLS v0.3", $0D, $0A
    .byte "DISASSEMBLER", $0D, $0A, $0A, $00
MSG_PROMPT:
    .byte "D XXXX = DISASSEMBLE   ESC = EXIT", $0D, $0A, $00

; Operand-rendering tables, indexed by addressing-mode id (must match the
; MODE_* order in opcodes_65c02.inc: IMP,ACC,IMM,ZP,ZPX,ZPY,ZPI,IZX,IZY,REL,
; ABS,ABX,ABY,IND,AIX,ZPR,UN1,UN2,UN3).
OPK:
    .byte KIND_NONE, KIND_ACC,  KIND_BYTE, KIND_BYTE, KIND_BYTE, KIND_BYTE
    .byte KIND_BYTE, KIND_BYTE, KIND_BYTE, KIND_REL,  KIND_WORD, KIND_WORD
    .byte KIND_WORD, KIND_WORD, KIND_WORD, KIND_ZPREL, KIND_NONE, KIND_NONE
    .byte KIND_NONE

OPPRE_L:
    .byte <S_EMPTY, <S_A,    <S_HASH, <S_DLR,  <S_DLR,  <S_DLR
    .byte <S_PAR,   <S_PAR,  <S_PAR,  <S_DLR,  <S_DLR,  <S_DLR
    .byte <S_DLR,   <S_PAR,  <S_PAR,  <S_DLR,  <S_EMPTY, <S_EMPTY
    .byte <S_EMPTY
OPPRE_H:
    .byte >S_EMPTY, >S_A,    >S_HASH, >S_DLR,  >S_DLR,  >S_DLR
    .byte >S_PAR,   >S_PAR,  >S_PAR,  >S_DLR,  >S_DLR,  >S_DLR
    .byte >S_DLR,   >S_PAR,  >S_PAR,  >S_DLR,  >S_EMPTY, >S_EMPTY
    .byte >S_EMPTY

OPSUF_L:
    .byte <S_EMPTY, <S_EMPTY, <S_EMPTY, <S_EMPTY, <S_COMMAX, <S_COMMAY
    .byte <S_RP,    <S_XRP,   <S_RPY,   <S_EMPTY, <S_EMPTY,  <S_COMMAX
    .byte <S_COMMAY, <S_RP,   <S_XRP,   <S_EMPTY, <S_EMPTY,  <S_EMPTY
    .byte <S_EMPTY
OPSUF_H:
    .byte >S_EMPTY, >S_EMPTY, >S_EMPTY, >S_EMPTY, >S_COMMAX, >S_COMMAY
    .byte >S_RP,    >S_XRP,   >S_RPY,   >S_EMPTY, >S_EMPTY,  >S_COMMAX
    .byte >S_COMMAY, >S_RP,   >S_XRP,   >S_EMPTY, >S_EMPTY,  >S_EMPTY
    .byte >S_EMPTY

S_EMPTY:  .byte $00
S_A:      .byte "A", $00
S_HASH:   .byte "#$", $00
S_DLR:    .byte "$", $00
S_PAR:    .byte "($", $00
S_COMMAX: .byte ",X", $00
S_COMMAY: .byte ",Y", $00
S_RP:     .byte ")", $00
S_XRP:    .byte ",X)", $00
S_RPY:    .byte "),Y", $00

; Canonical 65C02 opcode/addressing-mode table (generated from CPU6502).
.include "opcodes_65c02.inc"
