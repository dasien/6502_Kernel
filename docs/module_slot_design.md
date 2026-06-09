# Bankable Module Slot — Design

**Status:** Phases 1–3 implemented (kernel v3.0). I/O is at `$FE00`, the module
window is a clean bank-switched slot (`MODULE_BANK` `$FE23`), and **BASIC is module
bank 1** — the host installs `basic.rom` as a bank, `B:` is the module bank menu
(driven by the kernel `MODULE_DIR` catalog), modules return via `$FF12`
(`RETURN_FROM_MODULE`, which unmaps the bank), and `RESET` zeroes the window so
bank 0 boots clean. Phase 4 (a first new module: assembler + disassembler) is open.

## Goal

Stop growing (or shrinking) the kernel ROM to add big features. Instead, make the
12 KB region currently occupied by EhBASIC a **bank-switched module window**: a slot
into which the kernel maps one ROM "module" at a time (BASIC, an assembler/
disassembler package, a Z-machine to play Zork, a text editor, …). BASIC becomes
just *one* module rather than a permanent resident.

This mirrors how real 6502 machines did it — cartridge ROMs, bank-switched ROM,
the Apple II language card.

### Why not just grow / shrink the kernel?

- A debugger-grade monitor wants a disassembler (~1–1.5 KB) and mini-assembler
  (~1–1.5 KB). Those don't belong in the always-resident kernel.
- Shrinking the kernel to 4 KB to reclaim user RAM would be undone the moment we
  add a disassembler. Modules sidestep the whole question.

The kernel stays at `$E000–$FFFF` (8 KB), unchanged in start address.

## Memory map (target)

```
$0000–$07FF   Zero page / stack / system vars / screen      (unchanged)
$0800–$AFFF   User RAM (~42 KB)                              (module working RAM)
$B000–$DFFF   MODULE WINDOW (12 KB) — backed by selected bank; clean, no I/O hole
$E000–$EEC3   Kernel CODE (~3.7 KB today)                    (start unchanged)
$EEC4–$FDFF   free kernel ROM (~3.9 KB)                      (kernel growth room)
$FE00–$FEFF   I/O page (relocated here from $DC00)
$FF00–$FFF9   Kernel API jump table (grows upward; ~83 entries possible, 7 used)
$FFFA–$FFFF   NMI / RESET / IRQ vectors
```

Key property: **the module window contains no I/O** — any ROM assembled at `$B000`
runs in a clean, contiguous 12 KB with no addresses to avoid.

## Prerequisite: relocate I/O out of the module window (`$DC00` → `$FE00`)

Today the PIA/file-I/O lives at `$DC00–$DC22`, *inside* the module window (a vestige
of the C64-style map). That was tolerable when BASIC was the only, hand-authored
occupant. For arbitrary module ROMs we can't enforce a "don't touch this 36-byte
window" rule, so we remove the constraint by moving the I/O.

The I/O shadow doesn't vanish — it moves from the module window (third-party ROM
territory) into the kernel ROM's unused space (our territory), where avoiding it is
trivial: the kernel's CODE ends at `$EEC3`, nowhere near `$FE00`. I/O is fixed at
**one page** (`$FE00–$FEFF`) — current usage is ~36 registers and even generous
expansion stays far under 256; page-aligned I/O is also natural to decode on real
hardware.

New I/O page layout (re-based 1:1 from the old `$DCxx` block):

| Addr | Register |
|------|----------|
| `$FE00` | `PIA_DATA` — keyboard data |
| `$FE02` | `PIA_CONTROL` |
| `$FE0E` | timer IRQ acknowledge |
| `$FE10` | `FILE_COMMAND` |
| `$FE11` | `FILE_STATUS` |
| `$FE12/$FE13` | `FILE_ADDR_LO/HI` |
| `$FE14–$FE1F` | `FILE_NAME_BUF` (12 bytes) |
| `$FE20/$FE21` | `FILE_END_ADDR_LO/HI` |
| `$FE22` | `FIO_DATA` — BASIC byte-stream LOAD/SAVE |
| `$FE23` | **`MODULE_BANK`** — bank-select register |

Touched by the relocation:
- `kernel.asm`: re-base the `PIA_*`, `FILE_*`, timer-ack equates.
- `basic.asm`: re-base `FIO_COMMAND`/`FIO_STATUS`/`FIO_DATA`.
- `src/computer/PIA.*` / `Memory`: update `isPiaAddress` (and any screen routing).
- `memory.cfg`: bound the `CODE` segment at `$FDFF` so the linker errors rather than
  growing into the I/O page.

Phase 1 (this relocation) is self-contained and worth doing on its own.

## Bank-select register — `MODULE_BANK = $FE23`

- **Write `n`:** map bank `n` into `$B000–$DFFF`.
  - `0` = RAM (slot is plain read/write RAM — the boot/default state).
  - `1…255` = read-only module ROM banks.
- **Read:** returns the current bank (kernel can save/restore).
- **Reset:** forced to `0`. **BASIC is not auto-loaded**; the slot starts empty.
- Lives in the always-mapped I/O page, so it's reachable regardless of what's mapped.

Bank capacity is bounded only by the register width: one byte → 256 banks × 12 KB
(~3 MB). We define a handful and leave the rest open.

## Emulator changes (`Memory`)

Bank-switched (not copy-on-demand): the host pre-loads each module image into a
`bankROM[1..N]` array at startup; switching is a pointer change — instant, and each
bank retains its own contents.

```
read(addr):
    if I/O addr ($FE00–$FEFF)              -> device / bank-register handler
    else if screen addr                    -> VIC
    else if $B000<=addr<=$DFFF and bank!=0  -> bankROM[bank][addr-$B000]   # read-only
    else                                   -> ram_[addr]

write(addr, v):
    if I/O addr                            -> device / bank-register handler
    else if screen addr                    -> VIC
    else if $B000<=addr<=$DFFF and bank!=0  -> ignored (ROM)
    else                                   -> ram_[addr]                    # bank 0 = RAM
```

## Module contract

A "module" is a 6502 ROM **ported to this system**:
1. Assembled to run from the module window (entry recorded in the directory below;
   `$B000` by default).
2. Reaches kernel services (character I/O, etc.) **only through the `$FF00` jump
   table** — the stable module ABI. (BASIC already does this via its `PG2_TABS`
   vectors.)
3. Returns to the monitor with `JMP $FF12` (`RETURN_FROM_MODULE`), which resets
   `MODULE_BANK = 0` and re-enters the command loop.
4. Uses `$0800–$AFFF` as working RAM, shared with all other modules → one tool at a
   time; "save your work before switching." Each module documents its RAM footprint.

A module is **not** required to reserve any specific bytes — there is no embedded
header or signature. Naming/entry metadata lives in the kernel (see below), so even
hard-to-modify third-party ROMs (a Z-machine, an off-the-shelf assembler) only need
the unavoidable port (re-base + retarget I/O), nothing more.

## Module directory (in the kernel ROM)

The kernel owns a curated catalog of known modules — like the `$FF00` jump table.
The `B:` menu and launcher read from it; the module ROMs stay untouched.

```
; One record per module: bank#, entry address, name pointer.
MODULE_DIR:
    .byte 1  : .word $B000 : .word NAME_BASIC      ; bank 1
    .byte 2  : .word $B000 : .word NAME_DEVTOOLS   ; bank 2
    .byte 3  : .word $B000 : .word NAME_ZORK       ; bank 3
MODULE_DIR_COUNT = 3

NAME_BASIC:    .byte "BASIC", 0
NAME_DEVTOOLS: .byte "ASSEMBLER / DISASSEMBLER", 0
NAME_ZORK:     .byte "ZORK (Z-MACHINE)", 0
```

Adding a module = add one record + name string, and add the ROM image to the host
bank set, then rebuild the kernel. The directory + names are tiny — well within the
~3.9 KB of kernel headroom.

## `B:` — Bank menu (replaces per-module commands)

`B:` is repurposed from "launch BASIC" to **"Bank"**: it lists the directory and lets
you pick a module to map + run.

```
BANKS:
  1  BASIC
  2  ASSEMBLER / DISASSEMBLER
  3  ZORK (Z-MACHINE)
  ?
```

- Build the menu by walking `MODULE_DIR` and printing each name.
- On a numeric selection: store the record's bank in `MODULE_BANK`, then `JMP`
  (record's entry address). ESC cancels back to the monitor.
- Adding a module never needs a new kernel command — it just appears in the menu.

```
; selection -> record index
LAUNCH_FROM_DIR:
    ; A = directory index chosen
    ; load bank#, entry from MODULE_DIR record
    STA MODULE_BANK          ; map the bank in
    JMP (entry)              ; run the module

; $FF12 handler
RETURN_FROM_MODULE:
    STZ MODULE_BANK          ; unmap (slot back to RAM)
    ...                      ; return to the monitor command loop
```

(Optional sanity byte-check after mapping is allowed but not required — the directory
is the source of truth.)

## Host-side bank registry

At startup the emulator loads module images into the bank table instead of writing
BASIC into flat RAM:
- bank 1 ← `basic.rom`
- bank 2 ← `devtools.rom`
- (3–255 reserved)

A small name→file map (config or convention). Bank 0 is RAM (no image).

## Settled decisions

1. **Naming/metadata → kernel-side `MODULE_DIR` table** (not embedded headers, no
   per-module signature). Works for hard-to-modify third-party ROMs; BASIC is just
   directory entry 1, no special-casing.
2. **First module → one combined "DEV TOOLS" ROM** (bank 2): assembler **and**
   disassembler together (they share the opcode/mnemonic tables).
3. **Feature placement → size-based split.** Big debugger machinery (disassembler,
   mini-assembler, single-step, breakpoints) lives in modules. Small always-useful
   commands (register display, hex add/subtract, memory compare) stay resident in the
   kernel.
4. **Bank 0 = RAM**, usable as scratch (not persistent across module loads).
5. **Module working RAM** = documented per-module footprint in `$0800–$AFFF`; one tool
   at a time, save before switching.
6. **`B:` = Bank menu**, replacing the old `B:` and any per-module command.

## Migration

- EhBASIC → **bank 1**, unchanged content (same `$B000` entry); the host registers it
  as a bank instead of loading it at boot. Its file-I/O equates move with the I/O
  relocation. It becomes directory entry 1.
- Kernel grows only: the I/O relocation, `MODULE_BANK` handling, `MODULE_DIR`, and the
  `B:` menu/launcher.

## Implementation phases

1. **[DONE, v2.2.7/8]** **Relocate I/O** `$DC00` → `$FE00` (kernel + basic + emulator),
   reserve the I/O page via an `IORESV` segment so the linker errors if `CODE` grows
   into it. Re-tested (integration suite + BASIC LOAD/SAVE). Window is now clean.
2. **[DONE, v2.2.9]** **Banking infrastructure**: `MODULE_BANK` register (`$FE23`) +
   `Memory` window routing (bank 0 = RAM, 1..255 = read-only ROM) + host bank table
   (`Memory::loadBank`). `RESET` maps the window to RAM. Behavior-preserving: BASIC
   still loads into bank-0 RAM at `$B000`. Covered by `tests/test_memory_banking.cpp`
   (11 cases) and the unchanged integration suite.
3. **[DONE, v3.0]** **Convert BASIC to bank 1**: the host installs `basic.rom` as a
   bank (`Memory::loadBank(1, …)`) instead of flat RAM. Added the kernel `MODULE_DIR`
   catalog + the `B:` bank menu/launcher; `RETURN_FROM_BASIC` became
   `RETURN_FROM_MODULE` (`$FF12`) and now unmaps the bank on exit. `RESET` zeroes
   `$B000–$DFFF` so bank 0 boots clean (safe now that BASIC is a ROM bank). Factored
   `FILL_RANGE_CORE` out of `F:` and reused it for the window clear. Covered by
   `testBankMenu`/`testBankLaunch` in the integration suite.
4. **First new module**: combined assembler + disassembler in bank 2.
