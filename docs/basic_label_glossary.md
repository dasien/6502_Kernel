# EhBASIC Label Glossary

A reference for the cryptic `LAB_<hex>` labels in `src/kernel/basic.asm`. The
interpreter — MFC BASIC — is derived from EhBASIC (see `NOTICE`). Rather than
rename ~780 code labels in place (a large, error-prone change), the labels are
left untouched and this file maps them to meaning.

**Descriptions are taken verbatim from the inline comments in `basic.asm`.** A
`LAB_<hex>` label's hex suffix is its assembled address (e.g. `LAB_1274` lives at
`$1274`). Most zero-page variables already have mnemonic names (`Bpntr`,
`Baslnl`, `FAC1`, …) and are documented in the source, so they are not repeated
here.

## Zero-page entry vectors / equates

| Label | Value | Meaning |
|-------|-------|---------|
| `LAB_WARM` | `$00` | BASIC warm start entry point |
| `LAB_IGBY` | `$BC` | "get next BASIC byte" subroutine (in zero page) |
| `LAB_GBYT` | `$C2` | "get current BASIC byte" at the text pointer |
| `LAB_STAK` | `$0100` | stack bottom (page 1), no offset |

## Named routine labels

| Label | Meaning |
|-------|---------|
| `LAB_A2HX` | convert A to ASCII hex byte and output .. note set decimal mode before calling |
| `LAB_ABS` | perform ABS() |
| `LAB_ADD` | add FAC2 to FAC1 |
| `LAB_AND` | perform AND |
| `LAB_ASC` | perform ASC() |
| `LAB_ATN` | perform ATN() |
| `LAB_AYFC` | save and convert integer AY to FAC1 |
| `LAB_BHSS` | process numeric expression(s) for BIN$ or HEX$ |
| `LAB_BINS` | perform BIN$() |
| `LAB_BITCLR` | perform BITCLR |
| `LAB_BITSET` | perform BITSET |
| `LAB_BTST` | perform BITTST() |
| `LAB_BYE` | This command exits BASIC and returns control to the monitor at $FF12 |
| `LAB_CALL` | perform CALL |
| `LAB_CASC` | check byte, return C=0 if<"A" or >"Z" or "a" to "z" |
| `LAB_CBIN` | get binary number |
| `LAB_CHEX` | get hex number |
| `LAB_CHRS` | perform CHR$() |
| `LAB_CKIN` | check whichever interrupt is indexed by X |
| `LAB_CKRN` | check not Direct (used by DEF and INPUT) |
| `LAB_CKTM` | type match check, set C for string, clear C for numeric |
| `LAB_CLEAR` | perform CLEAR |
| `LAB_COLD` | new page 2 initialisation, copy block to ccflag on |
| `LAB_CONT` | perform CONT |
| `LAB_COS` | perform COS() |
| `LAB_CRLF` | print CR/LF |
| `LAB_CTNM` | check if source is numeric, else do type mismatch |
| `LAB_CTST` | check if source is string, else do type mismatch |
| `LAB_DATA` | perform DATA |
| `LAB_DEC` | perform DEC |
| `LAB_DEEK` | perform DEEK() |
| `LAB_DEF` | perform DEF |
| `LAB_DIM` | perform DIM |
| `LAB_DO` | perform DO |
| `LAB_DOKE` | perform DOKE |
| `LAB_EOR` | pointers and offsets afterwards! |
| `LAB_EQUAL` | do = compare |
| `LAB_ESGL` | evaluate string, get length in Y |
| `LAB_EVBY` | evaluate byte expression, result in X |
| `LAB_EVEX` | evaluate expression |
| `LAB_EVIN` | evaluate integer expression |
| `LAB_EVIR` | evaluate integer expression (no sign check) |
| `LAB_EVNM` | evaluate expression and check is numeric, else do type mismatch |
| `LAB_EVPI` | evaluate integer expression (no check) |
| `LAB_EVST` | evaluate string |
| `LAB_EXP` | perform EXP()   (x^e) |
| `LAB_F2FX` | save unsigned 16 bit integer part of FAC1 in temporary integer |
| `LAB_FCER` | do function call error |
| `LAB_FOR` | perform FOR |
| `LAB_FRE` | perform FRE() |
| `LAB_FTBL` | action addresses for functions |
| `LAB_FTPL` | function pre process routine table |
| `LAB_GADB` | get two parameters for POKE or WAIT |
| `LAB_GARB` | garbage collection routine |
| `LAB_GET` | perform GET |
| `LAB_GFPN` | get fixed-point number into temp integer |
| `LAB_GMEM` | copy block from StrTab to $0000 - $0012 |
| `LAB_go_search` | search for line # in temp (Itempl/Itemph) from (AX) |
| `LAB_GOSUB` | perform GOSUB |
| `LAB_GOTO` | perform GOTO |
| `LAB_GTBY` | get byte parameter |
| `LAB_GTHAN` | do - FAC1 |
| `LAB_GVAL` | get value from line |
| `LAB_GVAR` | return pointer to variable in Cvaral/Cvarah |
| `LAB_HEXS` | perform HEX$() |
| `LAB_IF` | perform IF |
| `LAB_INC` | perform INC |
| `LAB_INLN` | print "? " and get BASIC input |
| `LAB_INPUT` | perform INPUT |
| `LAB_INT` | perform INT() |
| `LAB_IRQ` | perform IRQ {ON|OFF|CLEAR} |
| `LAB_KEYT` | note if length is 1 then the pointer is ignored |
| `LAB_LCASE` | perform LCASE$() |
| `LAB_LEFT` | perform LEFT$() |
| `LAB_LENS` | perform LEN() |
| `LAB_LET` | perform LET |
| `LAB_LIST` | bigger, faster version (a _lot_ faster) |
| `LAB_LOG` | perform LOG() |
| `LAB_LOOP` | perform LOOP |
| `LAB_LRMS` | process string for LEFT$, RIGHT$ or MID$ |
| `LAB_LSHIFT` | perform << (left shift) |
| `LAB_LTHAN` | do < compare |
| `LAB_MAX` | perform MAX() |
| `LAB_MIDS` | perform MID$() |
| `LAB_MIN` | perform MIN() |
| `LAB_MMEC` | check for correct exit, else so syntax error |
| `LAB_MSSP` | A=length, X=Sutill=ptr low byte, Y=Sutilh=ptr high byte |
| `LAB_NEW` | perform NEW |
| `LAB_NEXT` | perform NEXT |
| `LAB_NMI` | perform NMI {ON|OFF|CLEAR} |
| `LAB_no_ELSE` | following ELSE will, correctly, cause a syntax error |
| `LAB_NULL` | perform NULL |
| `LAB_OMER` | do "Out of memory" error then warm start |
| `LAB_ON` | perform ON |
| `LAB_OPPT` | hierarchy and action addresses for operator |
| `LAB_OR` | perform OR |
| `LAB_PEEK` | perform PEEK() |
| `LAB_PFAC` | pack FAC1 into (Lvarpl) |
| `LAB_PHFA` | this is the routine that does most of the work |
| `LAB_PI` | perform PI |
| `LAB_POKE` | perform POKE |
| `LAB_POS` | perform POS() |
| `LAB_POWER` | perform power function |
| `LAB_PPBI` | set numeric data type and increment BASIC execute pointer |
| `LAB_PPFN` | process numeric expression in parenthesis |
| `LAB_PPFS` | process string expression in parenthesis |
| `LAB_PRNA` | note! some routines expect this one to exit with Zb=0 |
| `LAB_READ` | perform READ |
| `LAB_REM` | perform REM, skip (rest of) line |
| `LAB_reset_search` | search for line # in temp (Itempl/Itemph) from start of mem pointer (Smeml) |
| `LAB_RESTORE` | perform RESTORE |
| `LAB_RETIRQ` | perform RETIRQ |
| `LAB_RETNMI` | perform RETNMI |
| `LAB_RETURN` | perform RETURN |
| `LAB_RIGHT` | perform RIGHT$() |
| `LAB_RND` | Serial correlation coefficient is -0.000370, totally uncorrelated would be 0.0 |
| `LAB_RSHIFT` | perform >> (right shift) |
| `LAB_RTST` | put string address and length on descriptor stack and update stack pointers |
| `LAB_RUN` | perform RUN |
| `LAB_SADD` | perform SADD() |
| `LAB_SCCA` | scan for CHR$(A) , else do syntax error then warm start |
| `LAB_SCGB` | scan for "," and get byte, else do Syntax error then warm start |
| `LAB_SGBY` | scan and get byte parameter |
| `LAB_SGN` | perform SGN() |
| `LAB_SHLN` | old 541 new 507 |
| `LAB_SIN` | perform SIN() |
| `LAB_SIRQ` | perform ON IRQ |
| `LAB_SNBL` | returns Y as index to [EOL] |
| `LAB_SNBS` | returns Y as index to [:] or [EOL] |
| `LAB_SNER` | syntax error then warm start |
| `LAB_SNMI` | perform ON NMI |
| `LAB_SQR` | perform SQR() |
| `LAB_SSLN` | search Basic for temp integer line number from start of mem |
| `LAB_STFA` | set exp=X, clearFAC1 mantissa3 and normalise |
| `LAB_STOP` | perform STOP |
| `LAB_STRS` | perform STR$() |
| `LAB_SUBTRACT` | perform subtraction, FAC1 from FAC2 |
| `LAB_SWAP` | perform SWAP |
| `LAB_TAN` | perform TAN() |
| `LAB_TWOPI` | perform TWOPI |
| `LAB_UCASE` | perform UCASE$() |
| `LAB_UFAC` | unpack memory (AY) into FAC1 |
| `LAB_USR` | perform USR() |
| `LAB_VAL` | perform VAL() |
| `LAB_VARPTR` | perform VARPTR() |
| `LAB_WAIT` | perform WAIT |
| `LAB_WDTH` | perform WIDTH |
| `LAB_XERR` | do error #X, then warm start |

## Numeric labels (`LAB_<hex>`, sorted by address)

| Label | Address | Meaning |
|-------|---------|---------|
| `LAB_11A1` | `$11A1` | exit with z=1 if FOR else exit with z=0 |
| `LAB_1212` | `$1212` | stack too deep? do OM error |
| `LAB_121F` | `$121F` | addr to check is in AY (low/high) |
| `LAB_1274` | `$1274` | wait for Basic command |
| `LAB_127D` | `$127D` | wait for Basic command (no "Ready") |
| `LAB_1295` | `$1295` | handle new BASIC line |
| `LAB_1357` | `$1357` | call for BASIC input (main entry point) |
| `LAB_138E` | `$138E` | announce buffer full |
| `LAB_13A6` | `$13A6` | faster, dictionary search version .... |
| `LAB_13D1` | `$13D1` | have matched first character of some keyword |
| `LAB_1477` | `$1477` | reset execution to start, clear vars and flush stack |
| `LAB_147A` | `$147A` | "CLEAR" command gets here |
| `LAB_1491` | `$1491` | flush stack and clear continue flag |
| `LAB_15C2` | `$15C2` | interpreter inner loop |
| `LAB_15FF` | `$15FF` | interpret BASIC code from (Bpntrl) |
| `LAB_1629` | `$1629` | key press is detected. |
| `LAB_1636` | `$1636` | if there was a key press it gets back here .. |
| `LAB_1696` | `$1696` | does RUN n |
| `LAB_16D0` | `$16D0` | search for line # in temp (Itempl/Itemph) from start of mem pointer (Smeml) |
| `LAB_16D4` | `$16D4` | search for line # in temp (Itempl/Itemph) from (AX) |
| `LAB_16F4` | `$16F4` | do the return without gosub error |
| `LAB_174E` | `$174E` | perform ELSE after IF |
| `LAB_1753` | `$1753` | found the matching ELSE, now do <{n|statement}> |
| `LAB_176B` | `$176B` | next character was GOTO or GOSUB |
| `LAB_17D5` | `$17D5` | string LET |
| `LAB_1829` | `$1829` | perform PRINT |
| `LAB_18C3` | `$18C3` | print null terminated string from memory |
| `LAB_18C6` | `$18C6` | print string from Sutill/Sutilh |
| `LAB_18E0` | `$18E0` | print " " |
| `LAB_18E3` | `$18E3` | print "?" character |
| `LAB_1904` | `$1904` | handle bad input data |
| `LAB_1B5B` | `$1B5B` | push sign, round FAC1 and put on stack |
| `LAB_1B78` | `$1B78` | do functions |
| `LAB_1BC1` | `$1BC1` | print "..." string to string util area |
| `LAB_1BD0` | `$1BD0` | do tokens |
| `LAB_1BFB` | `$1BFB` | scan for ")" , else do syntax error then warm start |
| `LAB_1BFE` | `$1BFE` | scan for "(" , else do syntax error then warm start |
| `LAB_1C01` | `$1C01` | scan for "," , else do syntax error then warm start |
| `LAB_1C11` | `$1C11` | set-up for functions |
| `LAB_1C18` | `$1C18` | get (var), return value in FAC_1 and $ flag |
| `LAB_1C27` | `$1C27` | for functions that returned strings |
| `LAB_1D82` | `$1D82` | check byte, return C=0 if<"A" or >"Z" |
| `LAB_1DE6` | `$1DE6` | set Adatal,Adatah to Astrtl,Astrth+2*Dimcnt+#$05 |
| `LAB_1E17` | `$1E17` | find or make array |
| `LAB_1E1F` | `$1E1F` | now get the array dimension(s) and stack it (them) before the data type and DIM flag |
| `LAB_1E5C` | `$1E5C` | no arrays). |
| `LAB_1E85` | `$1E85` | do array bounds error |
| `LAB_1F28` | `$1F28` | we have found, or built, the array. now we need to find the element |
| `LAB_1F7C` | `$1F7C` | does XY = (Astrtl),Y * (Asptl) |
| `LAB_1FD0` | `$1FD0` | convert Y to byte in FAC1 |
| `LAB_200B` | `$200B` | check FNx syntax |
| `LAB_2074` | `$2074` | restore Bpntrl,Bpntrh and function variable from stack |
| `LAB_207A` | `$207A` | put execute pointer and variable pointer into function |
| `LAB_209C` | `$209C` | copy des_pl/h to des_2l/h and make string space A bytes long |
| `LAB_20AE` | `$20AE` | print " terminated string to Sutill/Sutilh |
| `LAB_20B4` | `$20B4` | source is AY |
| `LAB_20F8` | `$20F8` | put string address and length on descriptor stack and update stack pointers |
| `LAB_2115` | `$2115` | return X=Sutill=ptr low byte, Y=Sutill=ptr high byte |
| `LAB_214B` | `$214B` | re-run routine from last ending |
| `LAB_21D1` | `$21D1` | return with XA = next variable pointer |
| `LAB_2216` | `$2216` | search complete, now either exit or set-up and move string |
| `LAB_224D` | `$224D` | add strings, string 1 is in descriptor des_pl, string 2 is in line |
| `LAB_228A` | `$228A` | copy string from descriptor (sdescr) to (Sutill) |
| `LAB_2298` | `$2298` | store string A bytes long from YX to (Sutill) |
| `LAB_229C` | `$229C` | store string A bytes long from (ut1_pl) to (Sutill) |
| `LAB_22B6` | `$22B6` | returns with A = length, X=pointer low byte, Y=pointer high byte |
| `LAB_22BA` | `$22BA` | returns with A = length, X=ut1_pl=pointer low byte, Y=ut1_ph=pointer high byte |
| `LAB_22EB` | `$22EB` | checks if AY is on the descriptor stack, if so does a stack discard |
| `LAB_236F` | `$236F` | return pointer in des_2l/h, byte in A (and X), Y=0 |
| `LAB_23A8` | `$23A8` | do function call error then warm start |
| `LAB_23F3` | `$23F3` | restore BASIC execute pointer from temp (Btmpl/Btmph) |
| `LAB_244E` | `$244E` | add 0.5 to FAC1 |
| `LAB_2455` | `$2455` | perform subtraction, FAC1 from (AY) |
| `LAB_2467` | `$2467` | perform addition |
| `LAB_246C` | `$246C` | add (AY) to FAC1 |
| `LAB_24D0` | `$24D0` | do ABS and normalise FAC1 |
| `LAB_24D5` | `$24D5` | normalise FAC1 |
| `LAB_24F1` | `$24F1` | clear FAC1 exponent and sign |
| `LAB_24F5` | `$24F5` | save FAC1 sign |
| `LAB_24F8` | `$24F8` | add FAC2 mantissa to FAC1 mantissa |
| `LAB_251B` | `$251B` | normalise FAC1 |
| `LAB_2528` | `$2528` | test and normalise FAC1 for C=0/1 |
| `LAB_252A` | `$252A` | normalise FAC1 for C=1 |
| `LAB_2537` | `$2537` | negate FAC1 |
| `LAB_253D` | `$253D` | twos complement FAC1 mantissa |
| `LAB_2559` | `$2559` | increment FAC1 mantissa |
| `LAB_2564` | `$2564` | do overflow error (overflow exit) |
| `LAB_2569` | `$2569` | shift FCAtemp << A+8 times |
| `LAB_257B` | `$257B` | shift FACX -A times right (> 8 shifts) |
| `LAB_2592` | `$2592` | shift FACX Y times right |
| `LAB_25FB` | `$25FB` | do convert AY, FCA1*(AY) |
| `LAB_264D` | `$264D` | unpack memory (AY) into FAC2 |
| `LAB_2673` | `$2673` | test and adjust accumulators |
| `LAB_2690` | `$2690` | handle overflow and underflow |
| `LAB_269E` | `$269E` | multiply by 10 |
| `LAB_26B9` | `$26B9` | divide by 10 |
| `LAB_26C2` | `$26C2` | divide by (AY) (X=sign) |
| `LAB_26CA` | `$26CA` | convert AY and do (AY)/FAC1 |
| `LAB_272B` | `$272B` | do A<<6, save as FAC1 rounding byte, normalise and return |
| `LAB_2737` | `$2737` | do "Divide by zero" error |
| `LAB_273C` | `$273C` | copy temp to FAC1 and normalise |
| `LAB_276E` | `$276E` | pack FAC1 into Adatal |
| `LAB_2778` | `$2778` | pack FAC1 into (XY) |
| `LAB_279B` | `$279B` | copy FAC2 to FAC1 |
| `LAB_279D` | `$279D` | save FAC1 sign and copy ABS(FAC2) to FAC1 |
| `LAB_27AB` | `$27AB` | round and copy FAC1 to FAC2 |
| `LAB_27AE` | `$27AE` | copy FAC1 to FAC2 |
| `LAB_27BA` | `$27BA` | round FAC1 |
| `LAB_27C2` | `$27C2` | round FAC1 (no check) |
| `LAB_27CA` | `$27CA` | return A=FF,C=1/-ve A=01,C=0/+ve |
| `LAB_27CE` | `$27CE` | no = 0 check |
| `LAB_27D0` | `$27D0` | no = 0 check, sign in A |
| `LAB_27DB` | `$27DB` | save A as integer byte |
| `LAB_27E3` | `$27E3` | set exp=X, clearFAC1 mantissa3 and normalise |
| `LAB_27F8` | `$27F8` | returns A=$FF if FAC1 < (AY) |
| `LAB_2828` | `$2828` | gets here if number <> FAC1 |
| `LAB_2831` | `$2831` | convert FAC1 floating-to-fixed |
| `LAB_2851` | `$2851` | shift FAC1 A times right |
| `LAB_287F` | `$287F` | clear FAC1 and return |
| `LAB_2887` | `$2887` | starting with "$" and "%" respectively |
| `LAB_289A` | `$289A` | get FAC1 from string .. first character wasn't numeric or - |
| `LAB_289C` | `$289C` | was "+" or "-" to start, so get next character |
| `LAB_289D` | `$289D` | code here for hex and binary numbers |
| `LAB_28A3` | `$28A3` | get FAC1 from string .. character wasn't numeric, -, +, hex or binary |
| `LAB_28FB` | `$28FB` | do - FAC1 and return |
| `LAB_28FE` | `$28FE` | do unsigned FAC1*10+number |
| `LAB_2912` | `$2912` | evaluate new ASCII digit |
| `LAB_2925` | `$2925` | evaluate next character of exponential part of number |
| `LAB_2953` | `$2953` | print " in line [LINE #]" |
| `LAB_295E` | `$295E` | print XA as unsigned integer |
| `LAB_296E` | `$296E` | not any more, moved scratchpad to page 0 |
| `LAB_29C0` | `$29C0` | now we have just the digits to do |
| `LAB_2A9A` | `$2A9A` | This table is used in converting numbers to ASCII. |
| `LAB_2B6E` | `$2B6E` | ^2 then series evaluation |
| `LAB_2B84` | `$2B84` | series evaluation |
| `LAB_2CEE` | `$2CEE` | increment and scan memory |
| `LAB_2CF4` | `$2CF4` | scan memory |
