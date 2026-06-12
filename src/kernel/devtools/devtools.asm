; ================================================================
; MFC 6502 DEV TOOLS MODULE  (assembler / disassembler)
; ================================================================
; Filename:     devtools.asm
; Author:       Brian Gentry
; Version:      0.6 (Phase 4 sub-step 6: host source load + listing)
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
; restored across the launch). Working RAM is $0800-$8FFF.
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
MON_CMDBUF_LEN   = 80               ; kernel command-buffer capacity
MON_CMDLEN       = $026A            ; length of the line in MON_CMDBUF

ASCII_ESC        = $1B
DISASM_COUNT     = 16               ; instructions disassembled per D command

; Byte-stream file I/O (host), same registers/protocol BASIC's LOAD uses.
FIO_COMMAND      = $FE10
FIO_STATUS       = $FE11
FIO_DATA         = $FE22
FIO_OPEN_RD      = $03              ; command: open input stream (host open dialog)
FIO_CLOSE        = $05              ; command: close stream
FIO_INPROG       = $01              ; status: operation in progress
FIO_EOF          = $04              ; status: no more bytes
FIO_ERROR        = $FF             ; status: error / cancelled

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
ASM_FORCE_ABS    = $D0              ; non-zero -> ASM_RESOLVE must use absolute

; ---- Two-pass assembler scratch ($C0-$DF; free while the module runs) -------
ASM2_PASS        = $D1              ; current pass (1 or 2)
ASM2_SRC         = $D2              ; source read pointer ($D2-$D3)
ASM2_LINENO      = $D4              ; source line number, for errors ($D4-$D5)
SYM_COUNT        = $D6              ; number of symbols defined
SYM_PTR          = $D7              ; symbol-table walk pointer ($D7-$D8)
ASM_HAS_SYM      = $D9              ; EVAL set this if the expr referenced a symbol
ASM2_TMP         = $DA              ; two-pass scratch
ASM2_HANDLED     = $DB              ; ASM2_TRYASSIGN: non-zero if it handled the line
ASM2_END         = $DC              ; set when .END seen (stop the pass)
EVAL_ACC         = $DD              ; expression accumulator ($DD-$DE)
ASM_BYTESEL      = $DF              ; 0=none, 1=low byte (<), 2=high byte (>)
ASM_LBL_BUF      = $C0              ; parsed identifier, 8 bytes space-padded ($C0-$C7)
ASM_NAME_BUF     = $C8              ; saved assignment name across EVAL ($C8-$CF)

; Two-pass memory layout (in user RAM; reserved while assembling). Lives just
; below the always-mapped DOS ROM at $9000 (user RAM tops out at $8FFF).
SRC_BUF          = $8000            ; source text buffer ($8000-$8FFF, $00-terminated)
SRC_BUF_END      = $8FFF            ; last buffer byte (reserved for the terminator)
SYM_TBL          = $7E00            ; symbol table ($7E00-$7FFF)
SYM_NAME_LEN     = 8                ; characters stored per symbol name
SYM_ENTRY_LEN    = 10               ; 8-byte name + 2-byte value
SYM_MAX          = 51               ; 512 bytes / 10
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
    CMP #'B'
    BEQ CMD_BUILD_T
    CMP #'L'
    BEQ CMD_LOAD_T
    BRA DEVT_ERROR                  ; unknown command
DEVT_EXIT:
    JMP K_RETURN_MODULE             ; unmaps bank 2, returns to the monitor
CMD_BUILD_T:                        ; trampolines (handlers are out of branch range)
    JMP CMD_BUILD
CMD_LOAD_T:
    JMP CMD_LOAD
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
; CMD_BUILD - "B": two-pass assemble the source buffer at $8000
; ----------------------------------------------------------------
CMD_BUILD:
    JSR ASM2_RUN                    ; carry set on error (message already printed)
    BCS CB_DONE
    LDA #<MSG_ASM_OK
    LDY #>MSG_ASM_OK
    JSR PUTS
CB_DONE:
    JMP DEVT_LOOP

; ----------------------------------------------------------------
; CMD_LOAD - "L": load a host source file into the source buffer ($8000) via the
; byte-stream file interface (the host shows an open dialog). Then "B" builds it.
; ----------------------------------------------------------------
CMD_LOAD:
    LDA #FIO_OPEN_RD
    STA FIO_COMMAND
CL_WAIT:
    LDA FIO_STATUS                  ; wait for the host to open the file
    CMP #FIO_INPROG
    BEQ CL_WAIT
    CMP #FIO_ERROR                  ; cancelled / failed
    BEQ CL_ERR
    LDA #<SRC_BUF                   ; write pointer = start of the source buffer
    STA ASM2_SRC
    LDA #>SRC_BUF
    STA ASM2_SRC+1
CL_READ:
    LDA FIO_STATUS
    CMP #FIO_EOF
    BEQ CL_EOF
    ; bounds: keep one byte for the terminator (stop at SRC_BUF_END = $8FFF)
    LDA ASM2_SRC+1
    CMP #>SRC_BUF_END
    BCC CL_STORE
    BNE CL_OVERFLOW                 ; past the source buffer
    LDA ASM2_SRC
    CMP #<SRC_BUF_END
    BCS CL_OVERFLOW                 ; at SRC_BUF_END -> reserve for terminator
CL_STORE:
    LDA FIO_DATA                    ; next byte from the stream
    LDY #$00
    STA (ASM2_SRC),Y
    INC ASM2_SRC
    BNE CL_READ
    INC ASM2_SRC+1
    BRA CL_READ
CL_EOF:
    LDA #$00                        ; terminate the source
    LDY #$00
    STA (ASM2_SRC),Y
    LDA #FIO_CLOSE
    STA FIO_COMMAND
    LDA #<MSG_LOADED
    LDY #>MSG_LOADED
    JSR PUTS
    JMP DEVT_LOOP
CL_OVERFLOW:
    LDA #$00                        ; terminate what we have, then report
    LDY #$00
    STA (ASM2_SRC),Y
CL_ERR:
    LDA #FIO_CLOSE
    STA FIO_COMMAND
    LDA #'?'
    JSR K_PRINT_CHAR
    JSR K_PRINT_NEWLINE
    JMP DEVT_LOOP

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
    ; Line-assembler policy: force absolute only when the value exceeds a byte.
    STZ ASM_FORCE_ABS
    LDA ASM_VAL+1
    STA ASM_FORCE_ABS               ; non-zero high byte -> absolute
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
    LDA ASM_FORCE_ABS
    BNE R_DIR_ABS                   ; force absolute (value > $FF, or a symbol ref)
    LDA #MODE_ZP
    JSR FIND_OPCODE
    BCC RES_STORE
R_DIR_ABS:
    LDA #MODE_ABS
    JMP R_FIND
R_DIRX:
    LDA ASM_FORCE_ABS
    BNE R_DIRX_ABS
    LDA #MODE_ZPX
    JSR FIND_OPCODE
    BCC RES_STORE
R_DIRX_ABS:
    LDA #MODE_ABX
    JMP R_FIND
R_DIRY:
    LDA ASM_FORCE_ABS
    BNE R_DIRY_ABS
    LDA #MODE_ZPY
    JSR FIND_OPCODE
    BCC RES_STORE
R_DIRY_ABS:
    LDA #MODE_ABY
    JMP R_FIND
R_IND:
    LDA ASM_FORCE_ABS
    BNE R_IND_ABS
    LDA #MODE_ZPI
    JSR FIND_OPCODE
    BCC RES_STORE
R_IND_ABS:
    LDA #MODE_IND
    JMP R_FIND
R_INDX:
    LDA ASM_FORCE_ABS
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

; ================================================================
; TWO-PASS ASSEMBLER (labels, .ORG/*=, .END; source pre-loaded at $8000)
; ================================================================

; ----------------------------------------------------------------
; ASM2_RUN - two passes over the source buffer. Pass 1 collects labels and sizes
; the code; pass 2 emits. Carry set on error (message already printed).
; ----------------------------------------------------------------
ASM2_RUN:
    STZ SYM_COUNT                   ; fresh symbol table; pass 1 fills it
    LDA #$01
    STA ASM2_PASS
    JSR ASM2_PASS_RUN
    BCS A2R_FAIL
    LDA #$02
    STA ASM2_PASS
    JSR ASM2_PASS_RUN
    BCS A2R_FAIL
    CLC
    RTS
A2R_FAIL:
    SEC
    RTS

; ----------------------------------------------------------------
; ASM2_PASS_RUN - run one pass over the whole source buffer
; ----------------------------------------------------------------
ASM2_PASS_RUN:
    LDA #<SRC_BUF
    STA ASM2_SRC
    LDA #>SRC_BUF
    STA ASM2_SRC+1
    STZ ASM2_LINENO
    STZ ASM2_LINENO+1
    STZ ASM2_END
    STZ ASM_PC                      ; default origin; .ORG/*= overrides
    STZ ASM_PC+1
APR_LOOP:
    JSR ASM2_NEXTLINE
    BCS APR_DONE                    ; end of source
    INC ASM2_LINENO
    BNE APR_NOHI
    INC ASM2_LINENO+1
APR_NOHI:
    LDA ASM2_PASS                   ; pass 2: list "AAAA: " (PC at line start)
    CMP #$02
    BNE APR_NOLIST
    LDA ASM_PC+1
    JSR K_PRINT_HEX_BYTE
    LDA ASM_PC
    JSR K_PRINT_HEX_BYTE
    LDA #':'
    JSR K_PRINT_CHAR
    LDA #' '
    JSR K_PRINT_CHAR
APR_NOLIST:
    JSR ASM2_LINE
    BCS APR_ERR
    LDA ASM2_PASS                   ; pass 2: list the source line text
    CMP #$02
    BNE APR_NOLIST2
    JSR LIST_LINE
APR_NOLIST2:
    LDA ASM2_END
    BNE APR_DONE                    ; .END stops the pass
    BRA APR_LOOP
APR_DONE:
    CLC
    RTS
APR_ERR:
    JSR ASM2_PRINT_ERR
    SEC
    RTS

; ----------------------------------------------------------------
; ASM2_NEXTLINE - copy the next source line into MON_CMDBUF, advance ASM2_SRC.
; Carry clear if a line was read (MON_CMDLEN set); carry set at end of source.
; ----------------------------------------------------------------
ASM2_NEXTLINE:
    LDY #$00                        ; offset from ASM2_SRC
    LDX #$00                        ; chars stored
ANL_LOOP:
    LDA (ASM2_SRC),Y
    BEQ ANL_EOF
    CMP #$0D
    BEQ ANL_EOL
    CMP #$0A
    BEQ ANL_EOL
    CPX #MON_CMDBUF_LEN-1
    BCS ANL_NOSTORE                 ; line too long: keep scanning, stop storing
    STA MON_CMDBUF,X
    INX
ANL_NOSTORE:
    INY
    BRA ANL_LOOP
ANL_EOL:
    INY                             ; consume this terminator
    LDA (ASM2_SRC),Y                ; consume a paired CR/LF
    CMP #$0A
    BEQ ANL_EOL2
    CMP #$0D
    BNE ANL_FINISH
ANL_EOL2:
    INY
ANL_FINISH:
    STX MON_CMDLEN
    TYA                             ; ASM2_SRC += Y
    CLC
    ADC ASM2_SRC
    STA ASM2_SRC
    BCC ANL_OK
    INC ASM2_SRC+1
ANL_OK:
    CLC
    RTS
ANL_EOF:
    CPX #$00
    BEQ ANL_END                     ; nothing left
    STX MON_CMDLEN                  ; final line without a terminator
    TYA
    CLC
    ADC ASM2_SRC
    STA ASM2_SRC
    BCC ANL_OK2
    INC ASM2_SRC+1
ANL_OK2:
    CLC
    RTS
ANL_END:
    SEC
    RTS

; ----------------------------------------------------------------
; ASM2_LINE - assemble one source line (in MON_CMDBUF) for the current pass.
; Carry set on error.
; ----------------------------------------------------------------
ASM2_LINE:
    JSR ASM2_STRIP_COMMENT
    STZ ASM_IDX
    JSR ASM2_SKIPSPC
    LDX ASM_IDX
    CPX MON_CMDLEN
    BCS AL_OK                       ; blank line
    JSR ASM2_TRYLABEL               ; defines a leading LABEL: (pass 1)
    BCS AL_ERR
    JSR ASM2_SKIPSPC
    LDX ASM_IDX
    CPX MON_CMDLEN
    BCS AL_OK                       ; label-only line
    LDA MON_CMDBUF,X
    CMP #'.'
    BEQ AL_PSEUDO
    CMP #'*'
    BEQ AL_PSEUDO
    JSR ASM2_TRYASSIGN              ; "NAME = expr"?
    BCS AL_ERR
    LDA ASM2_HANDLED
    BNE AL_OK                       ; assignment handled
    JMP ASM2_INSTR                  ; otherwise a mnemonic (tail)
AL_PSEUDO:
    JMP ASM2_PSEUDO                 ; tail
AL_OK:
    CLC
    RTS
AL_ERR:
    SEC
    RTS

; ASM2_STRIP_COMMENT - truncate MON_CMDLEN at the first ';'
ASM2_STRIP_COMMENT:
    LDX #$00
ASC_LOOP:
    CPX MON_CMDLEN
    BCS ASC_DONE
    LDA MON_CMDBUF,X
    CMP #';'
    BEQ ASC_CUT
    INX
    BRA ASC_LOOP
ASC_CUT:
    STX MON_CMDLEN
ASC_DONE:
    RTS

; ASM2_SKIPSPC - advance ASM_IDX past spaces in MON_CMDBUF
ASM2_SKIPSPC:
    LDX ASM_IDX
ASS_LOOP:
    CPX MON_CMDLEN
    BCS ASS_DONE
    LDA MON_CMDBUF,X
    CMP #' '
    BNE ASS_DONE
    INX
    BRA ASS_LOOP
ASS_DONE:
    STX ASM_IDX
    RTS

; ----------------------------------------------------------------
; ASM2_TRYLABEL - if the line begins with "IDENT:", define it (pass 1) and
; advance past ':'. If not, leave ASM_IDX unchanged. Carry set on error.
; ----------------------------------------------------------------
ASM2_TRYLABEL:
    LDA ASM_IDX
    STA ASM2_TMP                    ; remember the start (to rewind)
    LDX ASM_IDX
    LDA MON_CMDBUF,X
    JSR IS_ALPHA
    BCC TL_NO                       ; not a letter -> not a label
    JSR ASM2_PARSE_IDENT            ; -> ASM_LBL_BUF, ASM_IDX advanced
    BCS TL_ERR
    LDX ASM_IDX
    CPX MON_CMDLEN
    BCS TL_REWIND
    LDA MON_CMDBUF,X
    CMP #':'
    BNE TL_REWIND
    INC ASM_IDX                     ; consume ':'
    LDA ASM2_PASS
    CMP #$01
    BNE TL_OK                       ; pass 2: already defined
    LDA ASM_PC                      ; pass 1: define LABEL = current PC
    STA ASM_VAL
    LDA ASM_PC+1
    STA ASM_VAL+1
    JSR SYM_ADD
    BCS TL_ERR
TL_OK:
    CLC
    RTS
TL_REWIND:
    LDA ASM2_TMP                    ; the identifier was a mnemonic, not a label
    STA ASM_IDX
TL_NO:
    CLC
    RTS
TL_ERR:
    SEC
    RTS

; ----------------------------------------------------------------
; ASM2_TRYASSIGN - if the line is "NAME = expr", define NAME (pass 1) and set
; ASM2_HANDLED. If not an assignment, rewind ASM_IDX (ASM2_HANDLED = 0). Carry
; set on error.
; ----------------------------------------------------------------
ASM2_TRYASSIGN:
    STZ ASM2_HANDLED
    LDA ASM_IDX
    STA ASM2_TMP                    ; rewind point
    LDX ASM_IDX
    LDA MON_CMDBUF,X
    JSR IS_ALPHA
    BCC TA_NO
    JSR ASM2_PARSE_IDENT            ; name -> ASM_LBL_BUF
    BCS TA_ERR
    JSR ASM2_SKIPSPC
    LDX ASM_IDX
    CPX MON_CMDLEN
    BCS TA_REWIND
    LDA MON_CMDBUF,X
    CMP #'='
    BNE TA_REWIND
    INC ASM_IDX                     ; consume '='
    LDX #SYM_NAME_LEN-1             ; save name (EVAL may reuse ASM_LBL_BUF)
TA_SAVE:
    LDA ASM_LBL_BUF,X
    STA ASM_NAME_BUF,X
    DEX
    BPL TA_SAVE
    JSR ASM2_SKIPSPC
    JSR EVAL_EXPR
    BCS TA_ERR
    LDX #SYM_NAME_LEN-1             ; restore name for SYM_ADD
TA_REST:
    LDA ASM_NAME_BUF,X
    STA ASM_LBL_BUF,X
    DEX
    BPL TA_REST
    LDA ASM2_PASS
    CMP #$01
    BNE TA_HANDLED                  ; pass 2: already defined in pass 1
    JSR SYM_ADD
    BCS TA_ERR
TA_HANDLED:
    INC ASM2_HANDLED
    CLC
    RTS
TA_REWIND:
    LDA ASM2_TMP
    STA ASM_IDX
TA_NO:
    CLC
    RTS
TA_ERR:
    SEC
    RTS

; ----------------------------------------------------------------
; ASM2_PARSE_IDENT - parse an identifier at MON_CMDBUF[ASM_IDX] into ASM_LBL_BUF
; (8 bytes, space-padded), advance ASM_IDX. Carry set if empty or too long.
; ----------------------------------------------------------------
ASM2_PARSE_IDENT:
    LDX ASM_IDX
    LDY #$00
API_COPY:
    CPX MON_CMDLEN
    BCS API_PAD
    LDA MON_CMDBUF,X
    JSR IS_ALNUM
    BCC API_PAD
    CPY #SYM_NAME_LEN
    BCS API_ERR                     ; identifier too long
    STA ASM_LBL_BUF,Y
    INX
    INY
    BRA API_COPY
API_PAD:
    CPY #$00
    BEQ API_ERR                     ; empty
API_PAD_LOOP:
    CPY #SYM_NAME_LEN
    BCS API_OK
    LDA #' '
    STA ASM_LBL_BUF,Y
    INY
    BRA API_PAD_LOOP
API_OK:
    STX ASM_IDX
    CLC
    RTS
API_ERR:
    SEC
    RTS

; ----------------------------------------------------------------
; ASM2_INSTR - assemble a mnemonic + operand for the current pass
; ----------------------------------------------------------------
ASM2_INSTR:
    JSR ASM_PARSE_MNEM
    BCS AI2_ERR
    JSR ASM2_OPERAND
    BCS AI2_ERR
    LDA ASM_HAS_SYM                 ; force absolute for symbol refs or > $FF
    ORA ASM_VAL+1
    STA ASM_FORCE_ABS
    JSR ASM_RESOLVE
    BCS AI2_ERR
    LDA ASM2_PASS
    CMP #$02
    BEQ AI2_EMIT
    LDA ASM_PC                      ; pass 1: just advance PC by length
    CLC
    ADC ASM_LEN
    STA ASM_PC
    BCC AI2_OK
    INC ASM_PC+1
AI2_OK:
    CLC
    RTS
AI2_EMIT:
    JMP ASM_EMIT                    ; pass 2: emit (and advance ASM_PC); tail
AI2_ERR:
    SEC
    RTS

; ----------------------------------------------------------------
; ASM2_OPERAND - classify the operand (with expression values via EVAL_EXPR)
; into ASM_CLASS + ASM_VAL (+ ASM_HAS_SYM). Carry set on syntax error.
; ----------------------------------------------------------------
ASM2_OPERAND:
    STZ ASM_HAS_SYM
    LDX ASM_IDX
AO2_SKIP:
    CPX MON_CMDLEN
    BCS AO2_NONE
    LDA MON_CMDBUF,X
    CMP #' '
    BNE AO2_HAVE
    INX
    BRA AO2_SKIP
AO2_NONE:
    STX ASM_IDX
    LDA #CL_IMP
    STA ASM_CLASS
    CLC
    RTS
AO2_ERRJ:
    JMP AO2_ERR
AO2_HAVE:
    CMP #'#'
    BEQ AO2_IMM
    CMP #'('
    BEQ AO2_IND
    CMP #'A'
    BNE AO2_DIR
    INX                             ; maybe accumulator "A"
    CPX MON_CMDLEN
    BCS AO2_ACC
    LDA MON_CMDBUF,X
    CMP #' '
    BEQ AO2_ACC
    DEX                             ; "A..." -> a label; treat as direct
    BRA AO2_DIR
AO2_ACC:
    STX ASM_IDX
    LDA #CL_ACC
    STA ASM_CLASS
    CLC
    RTS
AO2_IMM:
    INX                             ; consume '#'
    STX ASM_IDX
    JSR EVAL_EXPR
    BCS AO2_ERRJ
    LDA #CL_IMM
    STA ASM_CLASS
    CLC
    RTS
AO2_DIR:
    STX ASM_IDX
    JSR EVAL_EXPR
    BCS AO2_ERRJ
    LDX ASM_IDX
    CPX MON_CMDLEN
    BCS AO2_DIR_PLAIN
    LDA MON_CMDBUF,X
    CMP #','
    BNE AO2_DIR_PLAIN
    INX
    CPX MON_CMDLEN
    BCS AO2_ERRJ
    LDA MON_CMDBUF,X
    CMP #'X'
    BEQ AO2_DIRX
    CMP #'Y'
    BEQ AO2_DIRY
    BRA AO2_ERRJ
AO2_DIR_PLAIN:
    STX ASM_IDX
    LDA #CL_DIR
    STA ASM_CLASS
    CLC
    RTS
AO2_DIRX:
    INX
    STX ASM_IDX
    LDA #CL_DIRX
    STA ASM_CLASS
    CLC
    RTS
AO2_DIRY:
    INX
    STX ASM_IDX
    LDA #CL_DIRY
    STA ASM_CLASS
    CLC
    RTS
AO2_IND:
    INX                             ; consume '('
    STX ASM_IDX
    JSR EVAL_EXPR
    BCS AO2_ERR
    LDX ASM_IDX
    CPX MON_CMDLEN
    BCS AO2_ERR
    LDA MON_CMDBUF,X
    CMP #')'
    BEQ AO2_IND_CLOSE
    CMP #','
    BNE AO2_ERR
    INX
    CPX MON_CMDLEN
    BCS AO2_ERR
    LDA MON_CMDBUF,X
    CMP #'X'
    BNE AO2_ERR
    INX
    CPX MON_CMDLEN
    BCS AO2_ERR
    LDA MON_CMDBUF,X
    CMP #')'
    BNE AO2_ERR
    INX
    STX ASM_IDX
    LDA #CL_INDX
    STA ASM_CLASS
    CLC
    RTS
AO2_IND_CLOSE:
    INX                             ; consume ')'
    CPX MON_CMDLEN
    BCS AO2_IND_PLAIN
    LDA MON_CMDBUF,X
    CMP #','
    BNE AO2_IND_PLAIN
    INX
    CPX MON_CMDLEN
    BCS AO2_ERR
    LDA MON_CMDBUF,X
    CMP #'Y'
    BNE AO2_ERR
    INX
    STX ASM_IDX
    LDA #CL_INDY
    STA ASM_CLASS
    CLC
    RTS
AO2_IND_PLAIN:
    STX ASM_IDX
    LDA #CL_IND
    STA ASM_CLASS
    CLC
    RTS
AO2_ERR:
    SEC
    RTS

; ----------------------------------------------------------------
; ASM2_PSEUDO - handle .ORG / *= / .END  (more directives in sub-step 5c)
; ----------------------------------------------------------------
ASM2_PSEUDO:
    LDX ASM_IDX
    LDA MON_CMDBUF,X
    CMP #'*'
    BEQ AP_STAR
    INC ASM_IDX                     ; consume '.'
    JSR ASM2_PARSE_IDENT            ; directive word -> ASM_LBL_BUF
    BCS AP_ERR
    LDA #<D_ORG
    LDY #>D_ORG
    JSR STREQ_LBL
    BCS AP_DO_ORG
    LDA #<D_END
    LDY #>D_END
    JSR STREQ_LBL
    BCS AP_DO_END
    LDA #<D_BYTE
    LDY #>D_BYTE
    JSR STREQ_LBL
    BCS AP_BYTE_J
    LDA #<D_DB
    LDY #>D_DB
    JSR STREQ_LBL
    BCS AP_BYTE_J
    LDA #<D_WORD
    LDY #>D_WORD
    JSR STREQ_LBL
    BCS AP_WORD_J
    LDA #<D_DW
    LDY #>D_DW
    JSR STREQ_LBL
    BCS AP_WORD_J
    LDA #<D_ASCII
    LDY #>D_ASCII
    JSR STREQ_LBL
    BCS AP_ASCII_J
    LDA #<D_TX
    LDY #>D_TX
    JSR STREQ_LBL
    BCS AP_ASCII_J
    BRA AP_ERR
AP_BYTE_J:
    JMP AP_DO_BYTE
AP_WORD_J:
    JMP AP_DO_WORD
AP_ASCII_J:
    JMP AP_DO_ASCII
AP_DO_END:
    INC ASM2_END
    CLC
    RTS
AP_STAR:
    INX                             ; consume '*'
    CPX MON_CMDLEN
    BCS AP_ERR
    LDA MON_CMDBUF,X
    CMP #'='
    BNE AP_ERR
    INX
    STX ASM_IDX
AP_DO_ORG:
    JSR ASM2_SKIPSPC
    JSR EVAL_EXPR
    BCS AP_ERR
    LDA ASM_VAL
    STA ASM_PC
    LDA ASM_VAL+1
    STA ASM_PC+1
    CLC
    RTS
AP_ERR:
    SEC
    RTS

; .BYTE/.DB - emit a comma-separated list of bytes
AP_DO_BYTE:
    JSR ASM2_SKIPSPC
    JSR EVAL_EXPR
    BCS ADB_ERR
    LDA ASM2_PASS
    CMP #$02
    BNE ADB_ADV
    LDA ASM_VAL
    LDY #$00
    STA (ASM_PC),Y
ADB_ADV:
    INC ASM_PC
    BNE ADB_COMMA
    INC ASM_PC+1
ADB_COMMA:
    JSR ASM2_SKIPSPC
    LDX ASM_IDX
    CPX MON_CMDLEN
    BCS ADB_DONE
    LDA MON_CMDBUF,X
    CMP #','
    BNE ADB_DONE
    INC ASM_IDX
    BRA AP_DO_BYTE
ADB_DONE:
    CLC
    RTS
ADB_ERR:
    SEC
    RTS

; .WORD/.DW - emit a comma-separated list of words (lo, hi)
AP_DO_WORD:
    JSR ASM2_SKIPSPC
    JSR EVAL_EXPR
    BCS ADB_ERR
    LDA ASM2_PASS
    CMP #$02
    BNE ADW_ADV
    LDA ASM_VAL
    LDY #$00
    STA (ASM_PC),Y
    LDA ASM_VAL+1
    LDY #$01
    STA (ASM_PC),Y
ADW_ADV:
    LDA ASM_PC
    CLC
    ADC #$02
    STA ASM_PC
    BCC ADW_COMMA
    INC ASM_PC+1
ADW_COMMA:
    JSR ASM2_SKIPSPC
    LDX ASM_IDX
    CPX MON_CMDLEN
    BCS ADW_DONE
    LDA MON_CMDBUF,X
    CMP #','
    BNE ADW_DONE
    INC ASM_IDX
    BRA AP_DO_WORD
ADW_DONE:
    CLC
    RTS

; .ASCII/.TX - emit the bytes of a "quoted" string
AP_DO_ASCII:
    JSR ASM2_SKIPSPC
    LDX ASM_IDX
    CPX MON_CMDLEN
    BCS ADB_ERR
    LDA MON_CMDBUF,X
    CMP #'"'
    BNE ADB_ERR
    INX                             ; skip opening quote
ADA_LOOP:
    CPX MON_CMDLEN
    BCS ADB_ERR                     ; unterminated string
    LDA MON_CMDBUF,X
    CMP #'"'
    BEQ ADA_END
    STA ASM2_TMP
    LDA ASM2_PASS
    CMP #$02
    BNE ADA_ADV
    LDA ASM2_TMP
    LDY #$00
    STA (ASM_PC),Y
ADA_ADV:
    INC ASM_PC
    BNE ADA_NEXT
    INC ASM_PC+1
ADA_NEXT:
    INX
    BRA ADA_LOOP
ADA_END:
    INX                             ; skip closing quote
    STX ASM_IDX
    CLC
    RTS

; STREQ_LBL - compare ASM_LBL_BUF (8 bytes) to the const string at A/Y.
; Carry set if equal, clear if not. (Uses MNE_PTR as scratch - free here.)
STREQ_LBL:
    STA MNE_PTR
    STY MNE_PTR+1
    LDY #$00
SEQ_LOOP:
    LDA (MNE_PTR),Y
    CMP ASM_LBL_BUF,Y
    BNE SEQ_NO
    INY
    CPY #SYM_NAME_LEN
    BCC SEQ_LOOP
    SEC
    RTS
SEQ_NO:
    CLC
    RTS

; ----------------------------------------------------------------
; EVAL_EXPR - evaluate an expression at MON_CMDBUF[ASM_IDX] -> ASM_VAL.
; Grammar: ['<'|'>'] term {('+'|'-') term}.  '<'/'>' select the low/high byte of
; the whole expression. Sets ASM_HAS_SYM if any symbol/PC was referenced. Advances
; ASM_IDX. Carry set on error (bad token / undefined symbol in pass 2).
; ----------------------------------------------------------------
EVAL_EXPR:
    STZ ASM_HAS_SYM
    STZ ASM_BYTESEL
    LDX ASM_IDX
    CPX MON_CMDLEN
    BCC EX_START
EX_ERR:
    SEC
    RTS
EX_START:
    LDA MON_CMDBUF,X
    CMP #'<'
    BEQ EX_LOW
    CMP #'>'
    BEQ EX_HIGH
    BRA EX_FIRST
EX_LOW:
    LDA #$01
    STA ASM_BYTESEL
    INC ASM_IDX
    BRA EX_FIRST
EX_HIGH:
    LDA #$02
    STA ASM_BYTESEL
    INC ASM_IDX
EX_FIRST:
    JSR EVAL_TERM
    BCS EX_ERR
    LDA ASM_VAL
    STA EVAL_ACC
    LDA ASM_VAL+1
    STA EVAL_ACC+1
EX_OPLOOP:
    LDX ASM_IDX
    CPX MON_CMDLEN
    BCS EX_FINISH
    LDA MON_CMDBUF,X
    CMP #'+'
    BEQ EX_ADD
    CMP #'-'
    BEQ EX_SUB
    BRA EX_FINISH
EX_ADD:
    INC ASM_IDX
    JSR EVAL_TERM
    BCS EX_ERR
    LDA EVAL_ACC
    CLC
    ADC ASM_VAL
    STA EVAL_ACC
    LDA EVAL_ACC+1
    ADC ASM_VAL+1
    STA EVAL_ACC+1
    BRA EX_OPLOOP
EX_SUB:
    INC ASM_IDX
    JSR EVAL_TERM
    BCS EX_ERR
    LDA EVAL_ACC
    SEC
    SBC ASM_VAL
    STA EVAL_ACC
    LDA EVAL_ACC+1
    SBC ASM_VAL+1
    STA EVAL_ACC+1
    BRA EX_OPLOOP
EX_FINISH:
    LDA EVAL_ACC
    STA ASM_VAL
    LDA EVAL_ACC+1
    STA ASM_VAL+1
    LDA ASM_BYTESEL
    BEQ EX_DONE
    CMP #$02
    BEQ EX_HISEL
    STZ ASM_VAL+1                   ; '<' low byte
    BRA EX_DONE
EX_HISEL:
    LDA ASM_VAL+1                   ; '>' high byte
    STA ASM_VAL
    STZ ASM_VAL+1
EX_DONE:
    CLC
    RTS

; EVAL_TERM - one term -> ASM_VAL; advances ASM_IDX; INC ASM_HAS_SYM for symbol/PC.
EVAL_TERM:
    LDX ASM_IDX
    CPX MON_CMDLEN
    BCS ET_ERR
    LDA MON_CMDBUF,X
    CMP #'$'
    BEQ ET_HEX
    CMP #'*'
    BEQ ET_STAR
    JSR IS_DIGIT
    BCS ET_DEC
    LDA MON_CMDBUF,X
    JSR IS_ALPHA
    BCS ET_LABEL
ET_ERR:
    SEC
    RTS
ET_HEX:
    INX                             ; consume '$'
    JSR ASM_HEX
    BCS ET_ERR
    STX ASM_IDX
    CLC
    RTS
ET_DEC:
    JSR ASM_DECIMAL
    BCS ET_ERR
    STX ASM_IDX
    CLC
    RTS
ET_STAR:
    INX
    STX ASM_IDX
    LDA ASM_PC
    STA ASM_VAL
    LDA ASM_PC+1
    STA ASM_VAL+1
    INC ASM_HAS_SYM                 ; '*' is an address -> force absolute
    CLC
    RTS
ET_LABEL:
    JSR ASM2_PARSE_IDENT            ; -> ASM_LBL_BUF, ASM_IDX advanced
    BCS ET_ERR
    INC ASM_HAS_SYM
    JSR SYM_LOOKUP                  ; ASM_LBL_BUF -> ASM_VAL; carry set if not found
    BCC ET_OK
    LDA ASM2_PASS                   ; undefined: ok (0) in pass 1, error in pass 2
    CMP #$01
    BNE ET_ERR
    STZ ASM_VAL
    STZ ASM_VAL+1
ET_OK:
    CLC
    RTS

; ----------------------------------------------------------------
; ASM_DECIMAL - parse decimal digits at MON_CMDBUF[X] -> ASM_VAL, advance X
; Carry set if no digits.
; ----------------------------------------------------------------
ASM_DECIMAL:
    STZ ASM_VAL
    STZ ASM_VAL+1
    LDY #$00
AD_LOOP:
    CPX MON_CMDLEN
    BCS AD_DONE
    LDA MON_CMDBUF,X
    JSR IS_DIGIT
    BCC AD_DONE
    ; ASM_VAL = ASM_VAL * 10 + (digit)
    LDA ASM_VAL
    STA ASM_DLO
    LDA ASM_VAL+1
    STA ASM_DHI                     ; t = ASM_VAL
    ASL ASM_VAL
    ROL ASM_VAL+1                   ; *2
    ASL ASM_VAL
    ROL ASM_VAL+1                   ; *4
    LDA ASM_VAL
    CLC
    ADC ASM_DLO
    STA ASM_VAL
    LDA ASM_VAL+1
    ADC ASM_DHI
    STA ASM_VAL+1                   ; *5
    ASL ASM_VAL
    ROL ASM_VAL+1                   ; *10
    LDA MON_CMDBUF,X
    SEC
    SBC #'0'
    CLC
    ADC ASM_VAL
    STA ASM_VAL
    BCC AD_NOC
    INC ASM_VAL+1
AD_NOC:
    INX
    INY
    BRA AD_LOOP
AD_DONE:
    CPY #$00
    BEQ AD_ERR
    CLC
    RTS
AD_ERR:
    SEC
    RTS

; ----------------------------------------------------------------
; Symbol table (name = 8 bytes, value = 2 bytes) at SYM_TBL.
; ----------------------------------------------------------------
; SYM_ADD - add ASM_LBL_BUF = ASM_VAL. Carry set if duplicate or table full.
SYM_ADD:
    LDA SYM_COUNT
    CMP #SYM_MAX
    BCS SA_ERR                      ; table full
    JSR SYM_LOOKUP                  ; duplicate?
    BCC SA_ERR                      ; found -> redefinition
    LDA SYM_COUNT
    JSR SYM_SET_PTR                 ; SYM_PTR = new entry
    LDY #$00
SA_NAME:
    LDA ASM_LBL_BUF,Y
    STA (SYM_PTR),Y
    INY
    CPY #SYM_NAME_LEN
    BCC SA_NAME
    LDA ASM_VAL
    STA (SYM_PTR),Y
    INY
    LDA ASM_VAL+1
    STA (SYM_PTR),Y
    INC SYM_COUNT
    CLC
    RTS
SA_ERR:
    SEC
    RTS

; SYM_LOOKUP - find ASM_LBL_BUF -> ASM_VAL. Carry clear if found, set if not.
SYM_LOOKUP:
    LDX #$00
SL_LOOP:
    CPX SYM_COUNT
    BCS SL_NO
    TXA
    JSR SYM_SET_PTR                 ; preserves X
    LDY #$00
SL_CMP:
    LDA (SYM_PTR),Y
    CMP ASM_LBL_BUF,Y
    BNE SL_NEXT
    INY
    CPY #SYM_NAME_LEN
    BCC SL_CMP
    LDY #SYM_NAME_LEN               ; matched -> read value
    LDA (SYM_PTR),Y
    STA ASM_VAL
    INY
    LDA (SYM_PTR),Y
    STA ASM_VAL+1
    CLC
    RTS
SL_NEXT:
    INX
    BRA SL_LOOP
SL_NO:
    SEC
    RTS

; SYM_SET_PTR - SYM_PTR = SYM_TBL + A*SYM_ENTRY_LEN. Preserves X.
SYM_SET_PTR:
    TAY
    LDA #<SYM_TBL
    STA SYM_PTR
    LDA #>SYM_TBL
    STA SYM_PTR+1
    CPY #$00
    BEQ SSP_DONE
SSP_LOOP:
    LDA SYM_PTR
    CLC
    ADC #SYM_ENTRY_LEN
    STA SYM_PTR
    BCC SSP_NC
    INC SYM_PTR+1
SSP_NC:
    DEY
    BNE SSP_LOOP
SSP_DONE:
    RTS

; ----------------------------------------------------------------
; Character classifiers (carry set if the char in A matches).
; ----------------------------------------------------------------
IS_DIGIT:
    CMP #'0'
    BCC ICL_NO
    CMP #'9'+1
    BCC ICL_YES
    CLC
    RTS
IS_ALPHA:
    CMP #'A'
    BCC ICL_NO
    CMP #'Z'+1
    BCC ICL_YES
ICL_NO:
    CLC
    RTS
ICL_YES:
    SEC
    RTS
IS_ALNUM:
    JSR IS_ALPHA
    BCS ICL_YES
    JSR IS_DIGIT
    RTS

; ----------------------------------------------------------------
; ASM2_PRINT_ERR - "? LINE nnnn" (line number in hex) + newline
; ----------------------------------------------------------------
ASM2_PRINT_ERR:
    LDA #<MSG_ASM_ERR
    LDY #>MSG_ASM_ERR
    JSR PUTS
    LDA ASM2_LINENO+1
    JSR K_PRINT_HEX_BYTE
    LDA ASM2_LINENO
    JSR K_PRINT_HEX_BYTE
    JMP K_PRINT_NEWLINE

; ----------------------------------------------------------------
; LIST_LINE - echo MON_CMDBUF (the current source line) + newline, for listings
; ----------------------------------------------------------------
LIST_LINE:
    LDX #$00
LL_LOOP:
    CPX MON_CMDLEN
    BCS LL_DONE
    LDA MON_CMDBUF,X
    JSR K_PRINT_CHAR                ; preserves X
    INX
    BRA LL_LOOP
LL_DONE:
    JMP K_PRINT_NEWLINE

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
    .byte "A XXXX=ASM LINE  L=LOAD  B=BUILD", $0D, $0A
    .byte "D XXXX=DISASSEMBLE  ESC=EXIT", $0D, $0A, $00
MSG_ASM_OK:
    .byte "OK", $0D, $0A, $00
MSG_ASM_ERR:
    .byte "? LINE ", $00
MSG_LOADED:
    .byte "LOADED", $0D, $0A, $00

; Directive names, 8 bytes space-padded (compared against ASM_LBL_BUF).
D_ORG:   .byte "ORG     "
D_END:   .byte "END     "
D_BYTE:  .byte "BYTE    "
D_DB:    .byte "DB      "
D_WORD:  .byte "WORD    "
D_DW:    .byte "DW      "
D_ASCII: .byte "ASCII   "
D_TX:    .byte "TX      "

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
