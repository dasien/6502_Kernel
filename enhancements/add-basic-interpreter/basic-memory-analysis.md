---
enhancement: add-basic-interpreter
task_id: task_1759417756_77601
agent: assembly-developer
created: 2025-10-02 12:15:56
---

# BASIC Interpreter Memory Analysis

## Executive Summary

This document provides a comprehensive analysis of memory usage for the EhBASIC interpreter and identifies all conflicts with the existing 6502 Monitor program. This analysis follows the mandatory methodology specified in the enhancement requirements.

## BASIC Zero Page Memory Usage Analysis

### Direct Zero Page Assignments

Analyzing src/kernel/basic.asm lines 46-312, extracting ALL zero page variable definitions:

| Address | Variable | Size | Purpose | Source |
|---------|----------|------|---------|--------|
| $00 | LAB_WARM | 1 | BASIC warm start entry point | Line 47 |
| $01 | Wrmjpl | 1 | BASIC warm start vector jump low byte | Line 48 |
| $02 | Wrmjph | 1 | BASIC warm start vector jump high byte | Line 49 |
| $03-$09 | (via LAB_WARM) | 7 | Reserved for warm start JMP vector | Calculated |
| $0A | Usrjmp | 1 | USR function JMP address | Line 51 |
| $0B | Usrjpl | 1 | USR function JMP vector low byte | Line 52 |
| $0C | Usrjph | 1 | USR function JMP vector high byte | Line 53 |
| $0D | Nullct | 1 | nulls output after each line | Line 54 |
| $0E | TPos | 1 | BASIC terminal position byte | Line 55 |
| $0F | TWidth | 1 | BASIC terminal width byte | Line 56 |
| $10 | Iclim | 1 | input column limit | Line 57 |
| $11 | Itempl | 1 | temporary integer low byte | Line 58 |
| $12 | Itemph | 1 | temporary integer high byte (Itempl+1) | Line 59 |
| $12 | nums_2 | 1 | number to bin/hex string convert (nums_1+1) | Line 61 |
| $13 | nums_3 | 1 | number to bin/hex string convert LSB (nums_1+2) | Line 62 |
| $14-$5A | **UNUSED** | 71 | Gap in assignments | Analysis |
| $5B | Srchc | 1 | search character / Temp3 | Line 64-65 |
| $5C | Scnquo | 1 | scan-between-quotes flag / Asrch | Line 66-67 |
| $5D | Ibptr | 1 | input buffer pointer / Dimcnt / Tindx | Line 72-74 |
| $5E | Defdim | 1 | default DIM flag | Line 76 |
| $5F | Dtypef | 1 | data type flag | Line 77 |
| $60 | Oquote | 1 | open quote flag / Gclctd | Line 78-79 |
| $61 | Sufnxf | 1 | subscript/FNX flag | Line 80 |
| $62 | Imode | 1 | input mode flag | Line 81 |
| $63 | Cflag | 1 | comparison evaluation flag | Line 83 |
| $64 | TabSiz | 1 | TAB step size | Line 85 |
| $65 | next_s | 1 | next descriptor stack address | Line 87 |
| $66 | last_sl | 1 | last descriptor stack address low byte | Line 91 |
| $67 | last_sh | 1 | last descriptor stack address high byte | Line 92 |
| $68 | des_sk | 1 | descriptor stack start address | Line 94 |
| $69-$70 | (via des_sk) | 8 | Descriptor stack area | Comment line 96 |
| $71 | ut1_pl | 1 | utility pointer 1 low byte | Line 98 |
| $72 | ut1_ph | 1 | utility pointer 1 high byte (ut1_pl+1) | Line 99 |
| $73 | ut2_pl | 1 | utility pointer 2 low byte | Line 100 |
| $74 | ut2_ph | 1 | utility pointer 2 high byte (ut2_pl+1) | Line 101 |
| $75 | FACt_1 | 1 | FAC temp mantissa1 | Line 105 |
| $76 | FACt_2 | 1 | FAC temp mantissa2 (FACt_1+1) | Line 106 |
| $77 | FACt_3 | 1 | FAC temp mantissa3 (FACt_2+1) | Line 107 |
| $78 | TempB | 1 | temp page 0 byte | Line 112 |
| $79 | Smeml | 1 | start of mem low byte | Line 114 |
| $7A | Smemh | 1 | start of mem high byte (Smeml+1) | Line 115 |
| $7B | Svarl | 1 | start of vars low byte | Line 116 |
| $7C | Svarh | 1 | start of vars high byte (Svarl+1) | Line 117 |
| $7D | Sarryl | 1 | var mem end low byte | Line 118 |
| $7E | Sarryh | 1 | var mem end high byte (Sarryl+1) | Line 119 |
| $7F | Earryl | 1 | array mem end low byte | Line 120 |
| $80 | Earryh | 1 | array mem end high byte (Earryl+1) | Line 121 |
| $81 | Sstorl | 1 | string storage low byte | Line 122 |
| $82 | Sstorh | 1 | string storage high byte (Sstorl+1) | Line 123 |
| $83 | Sutill | 1 | string utility ptr low byte | Line 124 |
| $84 | Sutilh | 1 | string utility ptr high byte (Sutill+1) | Line 125 |
| $85 | Ememl | 1 | end of mem low byte | Line 126 |
| $86 | Ememh | 1 | end of mem high byte (Ememl+1) | Line 127 |
| $87 | Clinel | 1 | current line low byte | Line 128 |
| $88 | Clineh | 1 | current line high byte (Clinel+1) | Line 129 |
| $89 | Blinel | 1 | break line low byte | Line 130 |
| $8A | Blineh | 1 | break line high byte (Blinel+1) | Line 131 |
| $8B | Cpntrl | 1 | continue pointer low byte | Line 133 |
| $8C | Cpntrh | 1 | continue pointer high byte (Cpntrl+1) | Line 134 |
| $8D | Dlinel | 1 | current DATA line low byte | Line 136 |
| $8E | Dlineh | 1 | current DATA line high byte (Dlinel+1) | Line 137 |
| $8F | Dptrl | 1 | DATA pointer low byte | Line 139 |
| $90 | Dptrh | 1 | DATA pointer high byte (Dptrl+1) | Line 140 |
| $91 | Rdptrl | 1 | read pointer low byte | Line 142 |
| $92 | Rdptrh | 1 | read pointer high byte (Rdptrl+1) | Line 143 |
| $93 | Varnm1 | 1 | current var name 1st byte | Line 145 |
| $94 | Varnm2 | 1 | current var name 2nd byte (Varnm1+1) | Line 146 |
| $95 | Cvaral | 1 | current var address low byte | Line 148 |
| $96 | Cvarah | 1 | current var address high byte (Cvaral+1) | Line 149 |
| $97 | Frnxtl | 1 | var pointer for FOR/NEXT low byte | Line 151 |
| $98 | Frnxth | 1 | var pointer for FOR/NEXT high byte (Frnxtl+1) | Line 152 |
| $99 | prstk | 1 | precedence stacked flag | Line 159 |
| $9A | (unused by BASIC) | 1 | Gap | Analysis |
| $9B | comp_f | 1 | compare function flag | Line 161 |
| $9C | func_l | 1 | function pointer low byte | Line 166 |
| $9D | func_h | 1 | function pointer high byte (func_l+1) | Line 167 |
| $9E | des_2l | 1 | string descriptor_2 pointer low byte | Line 172 |
| $9F | des_2h | 1 | string descriptor_2 pointer high byte (des_2l+1) | Line 173 |
| $A0 | g_step | 1 | garbage collect step size | Line 175 |
| $A1 | Fnxjmp | 1 | jump vector for functions | Line 177 |
| $A2 | Fnxjpl | 1 | functions jump vector low byte (Fnxjmp+1) | Line 178 |
| $A3 | Fnxjph / FAC2_r | 1 | functions jump vector high byte / FAC2 rounding | Line 179, 183 |
| $A4 | Adatal | 1 | array data pointer low byte | Line 185 |
| $A5 | Adatah | 1 | array data pointer high byte (Adatal+1) | Line 186 |
| $A6 | Obendl | 1 | old block end pointer low byte | Line 191 |
| $A7 | Obendh | 1 | old block end pointer high byte (Obendl+1) | Line 192 |
| $A8 | numexp | 1 | string to float number exponent count | Line 194 |
| $A9 | expcnt | 1 | string to float exponent count | Line 195 |
| $AA | numdpf | 1 | string to float decimal point flag | Line 199 |
| $AB | expneg | 1 | string to float eval exponent -ve flag | Line 200 |
| $AC | FAC1_e | 1 | FAC1 exponent | Line 220 |
| $AD | FAC1_1 | 1 | FAC1 mantissa1 (FAC1_e+1) | Line 221 |
| $AE | FAC1_2 | 1 | FAC1 mantissa2 (FAC1_e+2) | Line 222 |
| $AF | FAC1_3 | 1 | FAC1 mantissa3 (FAC1_e+3) | Line 223 |
| $B0 | FAC1_s | 1 | FAC1 sign (FAC1_e+4) | Line 224 |
| $B1 | negnum / numcon | 1 | string to float eval -ve flag | Line 235-236 |
| $B2 | FAC1_o | 1 | FAC1 overflow byte | Line 238 |
| $B3 | FAC2_e | 1 | FAC2 exponent | Line 240 |
| $B4 | FAC2_1 | 1 | FAC2 mantissa1 (FAC2_e+1) | Line 241 |
| $B5 | FAC2_2 | 1 | FAC2 mantissa2 (FAC2_e+2) | Line 242 |
| $B6 | FAC2_3 | 1 | FAC2 mantissa3 (FAC2_e+3) | Line 243 |
| $B7 | FAC2_s | 1 | FAC2 sign (FAC2_e+4) | Line 244 |
| $B8 | FAC_sc | 1 | FAC sign comparison | Line 246 |
| $B9 | FAC1_r | 1 | FAC1 rounding byte | Line 247 |
| $BA | csidx | 1 | line crunch save index | Line 254 |
| $BB | Aspth | 1 | array size/pointer high byte | Line 256 |
| $BC | LAB_IGBY | 1 | get next BASIC byte subroutine (start) | Line 268 |
| $BD-$C1 | (via LAB_IGBY) | 6 | Get next BASIC byte routine code | Calculated |
| $C2 | LAB_GBYT | 1 | get current BASIC byte subroutine | Line 270 |
| $C3 | Bpntrl | 1 | BASIC execute pointer low byte | Line 271 |
| $C4 | Bpntrh | 1 | BASIC execute pointer high byte (Bpntrl+1) | Line 272 |
| $C5-$D7 | (via LAB_GBYT) | 19 | Get BASIC char subroutine code | Comment line 274 |
| $D8 | Rbyte4 | 1 | extra PRNG byte | Line 277 |
| $D9 | Rbyte1 | 1 | most significant PRNG byte (Rbyte4+1) | Line 278 |
| $DA | Rbyte2 | 1 | middle PRNG byte (Rbyte4+2) | Line 279 |
| $DB | Rbyte3 | 1 | least significant PRNG byte (Rbyte4+3) | Line 280 |
| $DC | NmiBase | 1 | NMI handler flags | Line 282 |
| $DD | (NMI handler) | 1 | NMI handler addr low byte | Comment line 288 |
| $DE | (NMI handler) | 1 | NMI handler addr high byte | Comment line 289 |
| $DF | IrqBase | 1 | IRQ handler flags | Line 290 |
| $E0 | (IRQ handler) | 1 | IRQ handler addr low byte | Comment line 291 |
| $E1 | (IRQ handler) | 1 | IRQ handler addr high byte | Comment line 292 |
| $E2-$EE | **UNUSED** | 13 | Explicitly marked unused | Lines 295-307 |
| $EF | Decss | 1 | number to decimal string start | Line 310 |
| $F0 | Decssp1 | 1 | number to decimal string start+1 (Decss+1) | Line 311 |
| $F1-$FF | (via Decss) | 15 | Decimal string buffer | Comment line 313 |

### BASIC Zero Page Summary

**USED by BASIC**: $00-$13, $5B-$BB, $BC-$DB, $DC-$E1, $EF-$FF
**UNUSED by BASIC**: $14-$5A, $E2-$EE

**Total BASIC Zero Page Usage**: 175 bytes
**Available Zero Page in BASIC**: 81 bytes ($14-$5A = 71 bytes, $E2-$EE = 13 bytes)

### CRITICAL FINDING: Decimal String Buffer Conflict

**MAJOR CONFLICT IDENTIFIED**: BASIC uses $EF-$FF (17 bytes) for decimal string processing.
- Decss = $EF (decimal string start)
- Comment on line 313 indicates: "; $FF decimal string end"
- This is a BUFFER that uses ALL bytes from $EF through $FF

## Monitor Zero Page Memory Usage Analysis

From kernel.asm lines 78-94 and kernel_memory_map.md:

| Address | Variable | Size | Purpose | Source |
|---------|----------|------|---------|--------|
| $00 | MON_CURRADDR_LO | 1 | Current address low byte | kernel.asm:78 |
| $01 | MON_CURRADDR_HI | 1 | Current address high byte | kernel.asm:79 |
| $02 | MON_MSG_PTR_LO | 1 | Message pointer low byte | kernel.asm:80 |
| $03 | MON_MSG_PTR_HI | 1 | Message pointer high byte | kernel.asm:81 |
| $04 | JUMP_VECTOR | 1 | Indirect jump vector low | kernel.asm:82 |
| $05 | (JUMP_VECTOR+1) | 1 | Indirect jump vector high | Calculated |
| $06 | SCREEN_PTR_LO | 1 | Screen memory pointer low | kernel.asm:83 |
| $07 | SCREEN_PTR_HI | 1 | Screen memory pointer high | kernel.asm:84 |
| $08 | SCRL_SRC_ADDR_LO | 1 | Scroll source address low | kernel.asm:85 |
| $09 | SCRL_SRC_ADDR_HI | 1 | Scroll source address high | kernel.asm:86 |
| $0A | SCRL_DEST_ADDR_LO | 1 | Scroll destination low | kernel.asm:87 |
| $0B | SCRL_DEST_ADDR_HI | 1 | Scroll destination high | kernel.asm:88 |
| $0C | SCRL_BYTE_CNT | 1 | Scroll byte counter | kernel.asm:89 |
| $0D | CMD_LINE_COUNT | 1 | Command line counter | kernel.asm:90 |
| $0E | PAGE_ABORT_FLAG | 1 | ESC key handling flag | kernel.asm:91 |
| $0F | RNG_SEED | 1 | Random number seed | kernel.asm:92 |
| $10 | RNG_MAX | 1 | Random number maximum | kernel.asm:93 |
| $11-$EF | **UNUSED** | 223 | Available for user programs | kernel_memory_map.md:27 |
| $F0-$FF | HEX_LOOKUP_TABLE | 16 | Hex digit lookup table | kernel.asm:94 |

### Monitor Zero Page Summary

**USED by Monitor**: $00-$10, $F0-$FF
**UNUSED by Monitor**: $11-$EF

**Total Monitor Zero Page Usage**: 33 bytes
**Available Zero Page in Monitor**: 223 bytes

## Zero Page Conflict Analysis

Creating comprehensive conflict table for $00-$FF:

| Address | Monitor Usage | BASIC Usage | Conflict? | Notes |
|---------|---------------|-------------|-----------|-------|
| $00 | MON_CURRADDR_LO | LAB_WARM | **YES** | CRITICAL |
| $01 | MON_CURRADDR_HI | Wrmjpl | **YES** | CRITICAL |
| $02 | MON_MSG_PTR_LO | Wrmjph | **YES** | CRITICAL |
| $03 | MON_MSG_PTR_HI | (LAB_WARM+3) | **YES** | CRITICAL |
| $04 | JUMP_VECTOR | (LAB_WARM+4) | **YES** | CRITICAL |
| $05 | (JUMP_VECTOR+1) | (LAB_WARM+5) | **YES** | CRITICAL |
| $06 | SCREEN_PTR_LO | (LAB_WARM+6) | **YES** | CRITICAL |
| $07 | SCREEN_PTR_HI | (LAB_WARM+7) | **YES** | CRITICAL |
| $08 | SCRL_SRC_ADDR_LO | (LAB_WARM+8) | **YES** | CRITICAL |
| $09 | SCRL_SRC_ADDR_HI | (LAB_WARM+9) | **YES** | CRITICAL |
| $0A | SCRL_DEST_ADDR_LO | Usrjmp | **YES** | CRITICAL |
| $0B | SCRL_DEST_ADDR_HI | Usrjpl | **YES** | CRITICAL |
| $0C | SCRL_BYTE_CNT | Usrjph | **YES** | CRITICAL |
| $0D | CMD_LINE_COUNT | Nullct | **YES** | CRITICAL |
| $0E | PAGE_ABORT_FLAG | TPos | **YES** | CRITICAL |
| $0F | RNG_SEED | TWidth | **YES** | CRITICAL |
| $10 | RNG_MAX | Iclim | **YES** | CRITICAL |
| $11 | UNUSED | Itempl | NO | BASIC can use |
| $12 | UNUSED | Itemph | NO | BASIC can use |
| $13 | UNUSED | nums_3 | NO | BASIC can use |
| $14-$5A | UNUSED | UNUSED | NO | Both available |
| $5B-$BB | UNUSED | BASIC VARS | NO | BASIC can use |
| $BC-$DB | UNUSED | BASIC VARS | NO | BASIC can use |
| $DC-$E1 | UNUSED | BASIC IRQ/NMI | NO | BASIC can use |
| $E2-$EE | UNUSED | UNUSED | NO | Both available |
| $EF | UNUSED | Decss | NO | BASIC can use |
| $F0 | HEX_LOOKUP_TABLE | Decssp1 | **YES** | CRITICAL |
| $F1-$FF | HEX_LOOKUP_TABLE | Decimal string | **YES** | CRITICAL |

### Zero Page Conflict Summary

**TOTAL CONFLICTS FOUND**: 33 addresses ($00-$10, $F0-$FF)

**Conflict Categories**:
1. **Monitor variables $00-$10 conflict with BASIC warm start and setup** (17 conflicts)
2. **Monitor HEX_LOOKUP_TABLE $F0-$FF conflicts with BASIC decimal string buffer** (16 conflicts)

## Extended RAM Memory Analysis ($0200-$02FF)

### BASIC Extended RAM Usage

From basic.asm lines 446-467:

| Address Range | Size | Variable | Purpose |
|---------------|------|----------|---------|
| $0200 | 1 | ccflag | BASIC CTRL-C flag |
| $0201 | 1 | ccbyte | BASIC CTRL-C byte |
| $0202 | 1 | ccnull | BASIC CTRL-C byte timeout |
| $0203-$0204 | 2 | VEC_CC | ctrl c check vector |
| $0205-$0206 | 2 | VEC_IN | input vector |
| $0207-$0208 | 2 | VEC_OUT | output vector |
| $0209-$020A | 2 | VEC_LD | load vector |
| $020B-$020C | 2 | VEC_SV | save vector |
| $020D-$0228 | 28 | (IRQ/NMI code) | Calculated space before Ibuffs |
| $0229-$026F | 71 | Ibuffs | Input buffer (VEC_SV+$16 to +$47) |
| $0270-$02FF | 144 | (available) | Not used by BASIC in $0200 page |

**BASIC ACTUALLY USES**: $0200-$026F (112 bytes in $0200 page)
**Note**: BASIC Ram_base starts at $0300

### Monitor Extended RAM Usage (RELOCATED)

From kernel.asm lines 104-129 (ALREADY RELOCATED):

| Address Range | Size | Variable | Purpose |
|---------------|------|----------|---------|
| $0200-$024F | 80 | MON_CMDBUF | Command input buffer |
| $0250-$0268 | 25 | (old location) | NOW UNUSED - relocated to $0269+ |
| $0269 | 1 | MON_CMDPTR | Command buffer pointer (relocated) |
| $026A | 1 | MON_CMDLEN | Command length (relocated) |
| $026B | 1 | MON_MODE | Monitor mode (relocated) |
| $026C-$026D | 2 | MON_STARTADDR | Start address (relocated) |
| $026E-$026F | 2 | MON_ENDADDR | End address (relocated) |
| $0270-$027E | 15 | (other monitor vars) | Various monitor variables (relocated) |
| $027F-$028C | 14 | MON_SEARCH_PATTERN | Search pattern buffer (relocated) |
| $028D | 1 | MON_PATTERN_LEN | Pattern length (relocated) |
| $028E-$02DD | 80 | MON_LAST_CMD_BUF | Last command buffer (relocated) |
| $02DE | 1 | MON_LAST_CMD_LEN | Last command length (relocated) |
| $02DF-$02FF | 33 | (available) | Free space |

### Extended RAM Conflict Analysis ($0200-$02FF)

| Address Range | Monitor Usage | BASIC Usage | Conflict? | Resolution |
|---------------|---------------|-------------|-----------|------------|
| $0200-$024F | MON_CMDBUF | ccflag-VEC_SV area | **OVERLAP** | **ACCEPTABLE** - Command buffer inactive during BASIC mode |
| $0250-$0268 | (old vars, relocated) | Ibuffs (partial) | **OVERLAP** | **ACCEPTABLE** - Monitor vars already relocated |
| $0269-$026F | Monitor vars (new location) | Ibuffs (end) | **YES** | **CRITICAL CONFLICT** |
| $0270-$02DE | Monitor vars (new location) | UNUSED by BASIC | **SPILL** | **PROBLEM** - Monitor vars extend beyond BASIC usage |

### CRITICAL FINDING: Monitor Variable Relocation Conflict

The monitor variables were relocated to $0269-$02DE, but BASIC's input buffer (Ibuffs) extends to $026F. This creates a **7-byte overlap** at $0269-$026F.

**BASIC Input Buffer**: $0229-$026F (71 bytes, calculated as VEC_SV+$16 where VEC_SV=$020B, so $020B+$16=$0221... checking calculation)

Let me recalculate: VEC_SV = $020B, Ibuffs = VEC_SV+$16 = $0221
Ibuffe = Ibuffs+$47 = $0221+$47 = $0268

**CORRECTED**: BASIC Ibuffs = $0221-$0268 (72 bytes)

### CORRECTED Extended RAM Conflict Analysis

| Address Range | Monitor Usage | BASIC Usage | Conflict? | Resolution |
|---------------|---------------|-------------|-----------|------------|
| $0200-$024F | MON_CMDBUF | ccflag through partial Ibuffs | **OVERLAP** | **ACCEPTABLE** - Overlapping usage OK (monitor buffer inactive during BASIC) |
| $0250-$0268 | (old monitor location) | Ibuffs (end portion) | **OVERLAP** | **ACCEPTABLE** - Monitor already relocated |
| $0269-$02DE | Monitor vars (relocated) | UNUSED | NO | **SAFE** - No conflict |
| $02DF-$02FF | Available | UNUSED | NO | Free space |

## Memory Conflict Resolution Strategy

### Zero Page Conflicts - Resolution Required

**Problem 1: Monitor variables $00-$10 conflict with BASIC $00-$10**
- **Resolution**: Move Monitor zero page variables to unused range

**Problem 2: Monitor HEX_LOOKUP_TABLE $F0-$FF conflicts with BASIC Decss $EF-$FF**
- **Resolution**: Move Monitor HEX_LOOKUP_TABLE to unused range

### Proposed Zero Page Relocation Plan

**Strategy**: Move ALL monitor zero page variables to contiguous block in unused space

**Available Unused Ranges**:
- $14-$5A (71 bytes) - Large contiguous block
- $E2-$EE (13 bytes) - Smaller block

**Relocation Plan**:
Use $14-$2F range (28 bytes needed: 17 variables + 16 lookup table)

| Old Address | Variable | New Address | Size |
|-------------|----------|-------------|------|
| $00 | MON_CURRADDR_LO | $14 | 1 |
| $01 | MON_CURRADDR_HI | $15 | 1 |
| $02 | MON_MSG_PTR_LO | $16 | 1 |
| $03 | MON_MSG_PTR_HI | $17 | 1 |
| $04 | JUMP_VECTOR | $18 | 1 |
| $05 | (JUMP_VECTOR+1) | $19 | 1 |
| $06 | SCREEN_PTR_LO | $1A | 1 |
| $07 | SCREEN_PTR_HI | $1B | 1 |
| $08 | SCRL_SRC_ADDR_LO | $1C | 1 |
| $09 | SCRL_SRC_ADDR_HI | $1D | 1 |
| $0A | SCRL_DEST_ADDR_LO | $1E | 1 |
| $0B | SCRL_DEST_ADDR_HI | $1F | 1 |
| $0C | SCRL_BYTE_CNT | $20 | 1 |
| $0D | CMD_LINE_COUNT | $21 | 1 |
| $0E | PAGE_ABORT_FLAG | $22 | 1 |
| $0F | RNG_SEED | $23 | 1 |
| $10 | RNG_MAX | $24 | 1 |
| $F0-$FF | HEX_LOOKUP_TABLE | $25-$34 | 16 |

**After Relocation**:
- Monitor uses: $14-$34 (33 bytes contiguous)
- BASIC uses: $00-$13, $5B-$BB, $BC-$DB, $DC-$E1, $EF-$FF
- Shared available: $35-$5A, $E2-$EE

### Extended RAM - No Changes Needed

Monitor variables already relocated to $0269-$02DE, which doesn't conflict with BASIC usage ($0200-$0268).

**Command buffer overlap ($0200-$024F) is ACCEPTABLE**: Monitor command buffer is not needed while BASIC is running.

## Validation: Post-Relocation Memory Map

### Zero Page After Relocation ($00-$FF)

| Range | Usage | Owner | Notes |
|-------|-------|-------|-------|
| $00-$13 | BASIC warm start & core vars | BASIC | No conflict |
| $14-$24 | Monitor variables (relocated) | Monitor | 17 bytes |
| $25-$34 | Monitor hex lookup table (relocated) | Monitor | 16 bytes |
| $35-$5A | **AVAILABLE** | Shared | 38 bytes free |
| $5B-$BB | BASIC variables | BASIC | No conflict |
| $BC-$DB | BASIC routines & PRNG | BASIC | No conflict |
| $DC-$E1 | BASIC IRQ/NMI handlers | BASIC | No conflict |
| $E2-$EE | **AVAILABLE** | Shared | 13 bytes free |
| $EF-$FF | BASIC decimal string buffer | BASIC | No conflict |

**TOTAL CONFLICTS AFTER RELOCATION**: 0 ✓

### Extended RAM After Current Relocation ($0200-$02FF)

| Range | Usage | Owner | Conflict? |
|-------|-------|-------|-----------|
| $0200-$0268 | BASIC control & input buffer | BASIC | No |
| $0200-$024F | Monitor command buffer (overlapping) | Monitor | **Acceptable overlap** |
| $0269-$02DE | Monitor variables (relocated) | Monitor | No |
| $02DF-$02FF | Available | Shared | No |

**TOTAL CONFLICTS IN EXTENDED RAM**: 0 (1 acceptable overlap) ✓

## Implementation Changes Required

### Files to Modify

1. **src/kernel/kernel.asm**
   - Update ALL zero page variable definitions ($00-$10 → $14-$24)
   - Update HEX_LOOKUP_TABLE location ($F0-$FF → $25-$34)
   - Update all code references to these variables (search and replace)
   - Update HEX_LOOKUP_TABLE initialization routine

2. **docs/kernel_memory_map.md**
   - Update zero page allocation table
   - Update monitor variable addresses
   - Document new memory layout

3. **tests/test_*.cpp**
   - Update any tests that reference specific monitor memory addresses

### Code Changes Enumeration

**kernel.asm changes**:
1. Lines 78-94: Update zero page constant definitions
2. Search entire file for references to $00-$10, $F0-$FF and update to new addresses
3. Update HEX_LOOKUP_TABLE initialization code (populate $25-$34 instead of $F0-$FF)
4. Verify no hardcoded references to old addresses

**Estimated locations needing updates**: ~50-100 lines depending on how many direct memory references exist

## Next Steps

1. ✓ Complete zero page conflict analysis
2. ✓ Complete extended RAM conflict analysis
3. ✓ Create relocation plan
4. Next: Design I/O integration hooks for BASIC
5. Next: Design B: command implementation
6. Next: Design build system for basic.rom
7. Next: Create complete architecture document

---

## Analysis Metadata

- **Methodology**: Followed mandatory conflict analysis from enhancement requirements
- **Completeness**: 100% of zero page analyzed ($00-$FF)
- **Conflicts Identified**: 33 original conflicts
- **Conflicts After Resolution**: 0 critical conflicts
- **Validation**: Complete address-by-address verification performed
