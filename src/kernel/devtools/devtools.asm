; ================================================================
; MFC 6502 DEV TOOLS MODULE  (assembler / disassembler)
; ================================================================
; Filename:     devtools.asm
; Author:       Brian Gentry
; Version:      0.4 (Phase 4 sub-step 4: line assembler + disassembler)
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

; ---- Assembler scratch ($E0-$EF; the assembler and disassembler are separate
;      commands, so this block is independent of the DA_* zero page above) -----
ASM_MNE_BUF      = $E0              ; parsed mnemonic, 4 bytes space-padded ($E0-$E3)
ASM_DLO          = $E0              ; (reused after parsing) relative-offset diff lo
ASM_DHI          = $E1              ; relative-offset diff hi
ASM_PC           = $E4              ; assembly program counter ($E4-$E5)
ASM_VAL          = $E6              ; parsed operand value ($E6-$E7)
ASM_MNEM_ID      = $E8              ; mnemonic id
ASM_MODE         = $E9              ; resolved addressing-mode id
ASM_OPCODE       = $EA              ; resolved opcode
ASM_LEN          = $EB              ; instruction length
ASM_CLASS        = $EC              ; operand syntax class (CL_*)
ASM_IDX          = $ED              ; parse index into MON_CMDBUF
ASM_TMP          = $EE              ; FIND_OPCODE mode / scratch
ASM_TMP2         = $EF              ; hex-parse nibble scratch
ASM_PTR          = MNE_PTR          ; mnemonic-table search pointer (shared)

; Operand syntax classes (from PARSE_OPERAND; resolved to a mode by ASM_RESOLVE)
CL_IMP           = 0                ; (no operand)
CL_ACC           = 1                ; A
CL_IMM           = 2                ; #$nn
CL_DIR           = 3                ; $h        (-> ZP/ABS/REL)
CL_DIRX          = 4                ; $h,X      (-> ZPX/ABX)
CL_DIRY          = 5                ; $h,Y      (-> ZPY/ABY)
CL_IND           = 6                ; ($h)      (-> ZPI/IND)
CL_INDX          = 7                ; ($h,X)    (-> IZX/AIX)
CL_INDY          = 8                ; ($h),Y    (-> IZY)

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
    CMP #'A'
    BEQ CMD_ASM
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
; CMD_ASM - "A xxxx": interactive line assembler from xxxx. Prompts with the
; current address; each line is one instruction assembled to memory; an empty
; line or ESC exits assemble mode.
; ----------------------------------------------------------------
CMD_ASM:
    LDX #$01                        ; skip the command letter
CA_SKIP:
    CPX MON_CMDLEN
    BCS CA_ERR
    LDA MON_CMDBUF,X
    CMP #' '
    BNE CA_PARSE
    INX
    BRA CA_SKIP
CA_PARSE:
    JSR K_PARSE_HEX                 ; start address -> MON_CURRADDR
    BCS CA_ERR
    LDA MON_CURRADDR_LO
    STA ASM_PC
    LDA MON_CURRADDR_HI
    STA ASM_PC+1
CA_LINE:
    LDA ASM_PC+1                    ; address prompt "xxxx: "
    JSR K_PRINT_HEX_BYTE
    LDA ASM_PC
    JSR K_PRINT_HEX_BYTE
    LDA #':'
    JSR K_PRINT_CHAR
    LDA #' '
    JSR K_PRINT_CHAR
    JSR K_READ_LINE                 ; instruction line -> MON_CMDBUF
    LDA MON_CMDLEN
    BEQ CA_DONE                     ; empty line -> leave assemble mode
    LDA MON_CMDBUF
    CMP #ASCII_ESC
    BEQ CA_DONE                     ; ESC -> leave assemble mode
    JSR ASM_ONE                     ; assemble; carry set on error
    BCC CA_LINE
    LDA #'?'                        ; error: report and retry the same address
    JSR K_PRINT_CHAR
    JSR K_PRINT_NEWLINE
    BRA CA_LINE
CA_DONE:
    JMP DEVT_LOOP
CA_ERR:
    JMP DEVT_ERROR

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

; ================================================================
; ASSEMBLER (line-at-a-time, numeric operands; no labels/expressions yet)
; ================================================================

; ----------------------------------------------------------------
; ASM_ONE - assemble the instruction line in MON_CMDBUF to (ASM_PC), advance PC
; Output: carry clear on success (bytes written, ASM_PC advanced); carry set on
;         any error (bad mnemonic, operand syntax, illegal mode, branch range).
; ----------------------------------------------------------------
ASM_ONE:
    STZ ASM_IDX
    JSR ASM_PARSE_MNEM
    BCS AO_ERR
    JSR ASM_PARSE_OPERAND
    BCS AO_ERR
    JSR ASM_RESOLVE
    BCS AO_ERR
    JMP ASM_EMIT                    ; tail: emits and returns carry clear (or set on range)
AO_ERR:
    SEC
    RTS

; ----------------------------------------------------------------
; ASM_PARSE_MNEM - read the mnemonic from MON_CMDBUF[ASM_IDX], find its id
; Output: ASM_MNEM_ID set, ASM_IDX past the mnemonic; carry set if not found
; ----------------------------------------------------------------
ASM_PARSE_MNEM:
    LDX ASM_IDX
APM_SKIP:
    CPX MON_CMDLEN
    BCS APM_ERR                     ; nothing here
    LDA MON_CMDBUF,X
    CMP #' '
    BNE APM_COLLECT
    INX
    BRA APM_SKIP
APM_COLLECT:
    LDY #$00                        ; copy up to 4 chars into ASM_MNE_BUF
APM_COPY:
    CPX MON_CMDLEN
    BCS APM_PAD
    LDA MON_CMDBUF,X
    CMP #' '
    BEQ APM_PAD
    CPY #$04
    BCS APM_ERR                     ; mnemonic too long
    STA ASM_MNE_BUF,Y
    INX
    INY
    BRA APM_COPY
APM_PAD:
    CPY #$04                        ; space-pad to 4 bytes
    BCS APM_STORED
    LDA #' '
    STA ASM_MNE_BUF,Y
    INY
    BRA APM_PAD
APM_STORED:
    STX ASM_IDX                     ; advance past the mnemonic
    LDA #<MNEM_STR
    STA ASM_PTR
    LDA #>MNEM_STR
    STA ASM_PTR+1
    LDX #$00                        ; mnemonic id
APM_SEARCH:
    LDY #$00
APM_CMP:
    LDA (ASM_PTR),Y
    CMP ASM_MNE_BUF,Y
    BNE APM_NEXT
    INY
    CPY #$04
    BCC APM_CMP
    STX ASM_MNEM_ID                 ; matched
    CLC
    RTS
APM_NEXT:
    LDA ASM_PTR                     ; ASM_PTR += MNEM_WIDTH (4)
    CLC
    ADC #$04
    STA ASM_PTR
    BCC APM_NOC
    INC ASM_PTR+1
APM_NOC:
    INX
    CPX #MNEM_COUNT
    BCC APM_SEARCH
APM_ERR:
    SEC
    RTS

; ----------------------------------------------------------------
; ASM_PARSE_OPERAND - classify the operand from MON_CMDBUF[ASM_IDX]
; Output: ASM_CLASS set, ASM_VAL = parsed value; carry set on syntax error
; ----------------------------------------------------------------
ASM_PARSE_OPERAND:
    LDX ASM_IDX
APO_SKIP:
    CPX MON_CMDLEN
    BCS APO_NONE
    LDA MON_CMDBUF,X
    CMP #' '
    BNE APO_HAVE
    INX
    BRA APO_SKIP
APO_NONE:
    LDA #CL_IMP
    STA ASM_CLASS
    CLC
    RTS
APO_HAVE:
    CMP #'A'
    BEQ APO_ACC
    CMP #'#'
    BEQ APO_IMM
    CMP #'('
    BEQ APO_IND
    CMP #'$'
    BEQ APO_DIR
    SEC                             ; unrecognized operand
    RTS
APO_ACC:                            ; must be exactly "A" (end or space after)
    INX
    CPX MON_CMDLEN
    BCS APO_ACC_OK
    LDA MON_CMDBUF,X
    CMP #' '
    BNE APO_ERR
APO_ACC_OK:
    LDA #CL_ACC
    STA ASM_CLASS
    CLC
    RTS
APO_IMM:
    INX                             ; skip '#'
    CPX MON_CMDLEN
    BCS APO_ERR
    LDA MON_CMDBUF,X
    CMP #'$'
    BNE APO_ERR
    INX                             ; skip '$'
    JSR ASM_HEX
    BCS APO_ERR
    LDA #CL_IMM
    STA ASM_CLASS
    CLC
    RTS
APO_DIR:
    INX                             ; skip '$'
    JSR ASM_HEX
    BCS APO_ERR
    CPX MON_CMDLEN
    BCS APO_DIR_PLAIN
    LDA MON_CMDBUF,X
    CMP #','
    BNE APO_DIR_PLAIN
    INX                             ; skip ','
    CPX MON_CMDLEN
    BCS APO_ERR
    LDA MON_CMDBUF,X
    CMP #'X'
    BEQ APO_DIRX
    CMP #'Y'
    BEQ APO_DIRY
    SEC                             ; "$h,$..." (BBRn/BBSn) not supported yet
    RTS
APO_DIR_PLAIN:
    LDA #CL_DIR
    STA ASM_CLASS
    CLC
    RTS
APO_DIRX:
    LDA #CL_DIRX
    STA ASM_CLASS
    CLC
    RTS
APO_DIRY:
    LDA #CL_DIRY
    STA ASM_CLASS
    CLC
    RTS
; Error exit placed mid-routine so every branch above and below is in range.
APO_ERR:
    SEC
    RTS
APO_IND:
    INX                             ; skip '('
    CPX MON_CMDLEN
    BCS APO_ERR
    LDA MON_CMDBUF,X
    CMP #'$'
    BNE APO_ERR
    INX                             ; skip '$'
    JSR ASM_HEX
    BCS APO_ERR
    CPX MON_CMDLEN
    BCS APO_ERR
    LDA MON_CMDBUF,X
    CMP #')'
    BEQ APO_IND_CLOSE
    CMP #','
    BNE APO_ERR
    INX                             ; skip ','
    CPX MON_CMDLEN
    BCS APO_ERR
    LDA MON_CMDBUF,X
    CMP #'X'
    BNE APO_ERR
    INX                             ; skip 'X'
    CPX MON_CMDLEN
    BCS APO_ERR
    LDA MON_CMDBUF,X
    CMP #')'
    BNE APO_ERR
    LDA #CL_INDX                    ; ($h,X)
    STA ASM_CLASS
    CLC
    RTS
APO_IND_CLOSE:                      ; saw ')' -> ($h) or ($h),Y
    INX                             ; skip ')'
    CPX MON_CMDLEN
    BCS APO_IND_PLAIN
    LDA MON_CMDBUF,X
    CMP #','
    BNE APO_IND_PLAIN
    INX                             ; skip ','
    CPX MON_CMDLEN
    BCS APO_ERR
    LDA MON_CMDBUF,X
    CMP #'Y'
    BNE APO_ERR
    LDA #CL_INDY                    ; ($h),Y
    STA ASM_CLASS
    CLC
    RTS
APO_IND_PLAIN:
    LDA #CL_IND                     ; ($h)
    STA ASM_CLASS
    CLC
    RTS

; ----------------------------------------------------------------
; ASM_HEX - parse 1-4 hex digits at MON_CMDBUF[X] into ASM_VAL, advance X
; (The kernel only parses fixed 2-/4-digit fields; assembler operands are
;  variable length and must stop at the first non-hex character.)
; Output: ASM_VAL = value, X past the digits; carry set if no digits
; ----------------------------------------------------------------
ASM_HEX:
    STZ ASM_VAL
    STZ ASM_VAL+1
    LDY #$00                        ; digit count
AH_LOOP:
    CPX MON_CMDLEN
    BCS AH_DONE
    LDA MON_CMDBUF,X
    JSR ASM_NIBBLE
    BCC AH_DONE                     ; not a hex digit -> stop
    STA ASM_TMP2                    ; nibble
    ASL ASM_VAL
    ROL ASM_VAL+1
    ASL ASM_VAL
    ROL ASM_VAL+1
    ASL ASM_VAL
    ROL ASM_VAL+1
    ASL ASM_VAL
    ROL ASM_VAL+1
    LDA ASM_VAL
    ORA ASM_TMP2
    STA ASM_VAL
    INX
    INY
    BRA AH_LOOP
AH_DONE:
    CPY #$00
    BEQ AH_ERR                      ; no digits parsed
    CLC
    RTS
AH_ERR:
    SEC
    RTS

; ASM_NIBBLE - ASCII hex digit (A) -> nibble (A), carry set if valid
ASM_NIBBLE:
    CMP #'0'
    BCC AN_NO
    CMP #'9'+1
    BCC AN_DIGIT
    CMP #'A'
    BCC AN_NO
    CMP #'F'+1
    BCC AN_ALPHA
AN_NO:
    CLC
    RTS
AN_DIGIT:
    SEC
    SBC #'0'
    SEC
    RTS
AN_ALPHA:
    SEC
    SBC #('A'-10)
    SEC
    RTS

; ----------------------------------------------------------------
; FIND_OPCODE - find an opcode for (ASM_MNEM_ID, mode in A)
; Output: carry clear + A = opcode if found; carry set if no such combo.
;         Stores the searched mode in ASM_TMP (used by ASM_RESOLVE).
; ----------------------------------------------------------------
FIND_OPCODE:
    STA ASM_TMP
    LDX #$00
FO_LOOP:
    LDA OPC_MNEM,X
    CMP ASM_MNEM_ID
    BNE FO_NEXT
    LDA OPC_MODE,X
    CMP ASM_TMP
    BNE FO_NEXT
    TXA                             ; opcode = X
    CLC
    RTS
FO_NEXT:
    INX
    BNE FO_LOOP                     ; scan all 256 opcodes
    SEC
    RTS

; ----------------------------------------------------------------
; ASM_RESOLVE - pick the concrete mode + opcode for (ASM_CLASS, ASM_VAL, mnem)
; Output: ASM_OPCODE, ASM_MODE, ASM_LEN; carry set if the combo is illegal.
; ----------------------------------------------------------------
ASM_RESOLVE:
    LDA ASM_CLASS
    CMP #CL_IMP
    BEQ R_IMP
    CMP #CL_ACC
    BEQ R_ACC
    CMP #CL_IMM
    BEQ R_IMM
    CMP #CL_DIR
    BEQ R_DIR
    CMP #CL_DIRX
    BEQ R_DIRX
    CMP #CL_DIRY
    BEQ R_DIRY
    CMP #CL_IND
    BEQ R_IND
    CMP #CL_INDX
    BEQ R_INDX
    LDA #MODE_IZY                   ; CL_INDY
    JMP R_FIND
R_IMP:
    LDA #MODE_IMP
    JSR FIND_OPCODE
    BCC RES_STORE
    LDA #MODE_ACC                   ; fallback (e.g. "ASL" -> ASL A)
    JMP R_FIND
R_ACC:
    LDA #MODE_ACC
    JMP R_FIND
R_IMM:
    LDA #MODE_IMM
    JMP R_FIND
R_DIR:
    LDA #MODE_REL                   ; branches take a target address
    JSR FIND_OPCODE
    BCC RES_STORE
    LDA ASM_VAL+1
    BNE R_DIR_ABS                   ; value > $FF -> must be absolute
    LDA #MODE_ZP
    JSR FIND_OPCODE
    BCC RES_STORE
R_DIR_ABS:
    LDA #MODE_ABS
    JMP R_FIND
R_DIRX:
    LDA ASM_VAL+1
    BNE R_DIRX_ABS
    LDA #MODE_ZPX
    JSR FIND_OPCODE
    BCC RES_STORE
R_DIRX_ABS:
    LDA #MODE_ABX
    JMP R_FIND
R_DIRY:
    LDA ASM_VAL+1
    BNE R_DIRY_ABS
    LDA #MODE_ZPY
    JSR FIND_OPCODE
    BCC RES_STORE
R_DIRY_ABS:
    LDA #MODE_ABY
    JMP R_FIND
R_IND:
    LDA ASM_VAL+1
    BNE R_IND_ABS
    LDA #MODE_ZPI
    JSR FIND_OPCODE
    BCC RES_STORE
R_IND_ABS:
    LDA #MODE_IND
    JMP R_FIND
R_INDX:
    LDA ASM_VAL+1
    BNE R_INDX_ABS
    LDA #MODE_IZX
    JSR FIND_OPCODE
    BCC RES_STORE
R_INDX_ABS:
    LDA #MODE_AIX
    JMP R_FIND
R_FIND:
    JSR FIND_OPCODE
    BCS R_ERR
RES_STORE:                          ; A = opcode, ASM_TMP = searched mode
    STA ASM_OPCODE
    LDA ASM_TMP
    STA ASM_MODE
    TAX
    LDA MODE_LEN,X
    STA ASM_LEN
    CLC
    RTS
R_ERR:
    SEC
    RTS

; ----------------------------------------------------------------
; ASM_EMIT - write the instruction to (ASM_PC) and advance ASM_PC by ASM_LEN
; Output: carry clear on success; carry set if a relative branch is out of range
; ----------------------------------------------------------------
ASM_EMIT:
    LDA ASM_MODE
    CMP #MODE_REL
    BEQ AE_REL
    LDA ASM_OPCODE                  ; opcode byte
    LDY #$00
    STA (ASM_PC),Y
    LDA ASM_LEN
    CMP #$02
    BCC AE_ADV                      ; 1-byte instruction
    LDA ASM_VAL                     ; operand low byte
    LDY #$01
    STA (ASM_PC),Y
    LDA ASM_LEN
    CMP #$03
    BCC AE_ADV                      ; 2-byte instruction
    LDA ASM_VAL+1                   ; operand high byte
    LDY #$02
    STA (ASM_PC),Y
AE_ADV:
    LDA ASM_PC
    CLC
    ADC ASM_LEN
    STA ASM_PC
    BCC AE_OK
    INC ASM_PC+1
AE_OK:
    CLC
    RTS
AE_REL:
    ; offset = ASM_VAL - (ASM_PC + 2); must fit signed 8-bit
    LDA ASM_VAL
    SEC
    SBC ASM_PC
    STA ASM_DLO
    LDA ASM_VAL+1
    SBC ASM_PC+1
    STA ASM_DHI                     ; diff = target - PC
    LDA ASM_DLO
    SEC
    SBC #$02
    STA ASM_DLO
    LDA ASM_DHI
    SBC #$00
    STA ASM_DHI                     ; diff = target - (PC + 2)
    LDA ASM_DHI
    BEQ AE_REL_POS                  ; diff $00xx -> need xx <= $7F
    CMP #$FF
    BNE AE_REL_RANGE                ; not $00xx or $FFxx -> out of range
    LDA ASM_DLO
    CMP #$80
    BCC AE_REL_RANGE                ; $FFxx -> need xx >= $80
    BRA AE_REL_EMIT
AE_REL_POS:
    LDA ASM_DLO
    CMP #$80
    BCS AE_REL_RANGE
AE_REL_EMIT:
    LDA ASM_OPCODE
    LDY #$00
    STA (ASM_PC),Y
    LDA ASM_DLO
    LDY #$01
    STA (ASM_PC),Y
    LDA ASM_PC                      ; branches are always 2 bytes
    CLC
    ADC #$02
    STA ASM_PC
    BCC AE_REL_OK
    INC ASM_PC+1
AE_REL_OK:
    CLC
    RTS
AE_REL_RANGE:
    SEC
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
    .byte "MFC DEV TOOLS v0.4", $0D, $0A
    .byte "ASSEMBLER / DISASSEMBLER", $0D, $0A, $0A, $00
MSG_PROMPT:
    .byte "A XXXX=ASSEMBLE  D XXXX=DISASSEMBLE", $0D, $0A
    .byte "ESC=EXIT", $0D, $0A, $00

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
