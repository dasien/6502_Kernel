# MFC Kernel Internals

How the machine behaves at run time, traced as execution paths and call trees. The
kernel's interface is documented elsewhere. The `$FF00` jump table, the memory map and
the bank-switched module design are all in [architecture.md](architecture.md), and this
document is the implementation behind that.

This covers the kernel proper, which is the BIOS at `$F000-$FFFF`. The monitor is
module bank 4 rather than part of the kernel, so its execution flows and command call
trees live in [monitor_internals.md](monitor_internals.md).

The sections are written as records rather than prose, with a labelled field per line,
because they are meant to be scanned against the source rather than read through.

---

### System Boot Flow

#### 1. RESET (entry point, `$F000`)
**Path**: hardware reset vector → `RESET`
- **Sequential execution**: processor setup
  - `CLD` - clear decimal mode
  - `SEI` - disable interrupts
  - `LDX #STACK_TOP; TXS` - initialise the stack pointer
  - `STZ MODULE_BANK` - map the module window (`$B000-$EFFF`) to RAM, so the slot
    boots empty. Modules are mapped in later through the bank register
- **Flow continues to**: zero-page initialisation

There is no processor port on this machine. Banking is the `MODULE_BANK` register at
`$FE23` and nothing else, so reset has no direction register or bank byte to program.

#### 2. Zero-page clear loop
**Path**: `RESET` → `ZP_CLEAR_LOOP`
- **LOOP**: `ZP_CLEAR_LOOP`
  - **Entry condition**: X = `$00`
  - **Loop body**:
    - `STZ $00,X` - clear a zero-page location
    - `INX` - advance
  - **Exit condition**: `CPX #$F0; BNE ZP_CLEAR_LOOP`
  - **Iterations**: 240, clearing `$00-$EF`
- **Note**: it stops at `$F0` deliberately, leaving BASIC's high zero page
  (`$F0-$FF`) untouched
- **Flow continues to**: screen clear

#### 3. Screen clear
**Path**: `ZP_CLEAR_LOOP` → `CLEAR_SCREEN`
- **Sequential execution**: `JSR CLEAR_SCREEN`
  - sets `VREG_ATTR` to `$02` (green on black) and `VREG_CMD_PARAM` to a space
  - writes `VCMD_CLEAR` to `VREG_CMD`, so the chip clears its own planes
  - zeroes `CURSOR_X` and `CURSOR_Y`, then `JMP UPDATE_CURSOR`
- **Flow continues to**: module-window clear

This is a single command to the VIC, not a memory fill. The screen is not in the 64K
map, so there are no screen pages for the CPU to write over.

#### 4. Module-window clear
**Path**: `CLEAR_SCREEN` → private page loop
- **LOOP**: 48 pages, `$B0` through `$DF`
  - **Purpose**: bank 0 boots as clean scratch RAM. `MODULE_BANK` was zeroed above,
    so these writes land in window RAM rather than in any module ROM
- **Note**: the BIOS cannot call into the module window, so reset carries its own
  loop rather than reaching for the monitor's `F:` fill engine
- **Flow continues to**: RNG seeding

#### 5. RNG seed, pager defaults and handoff
**Path**: window clear → `DOS_COLD`
- **Sequential execution**:
  - `STA RTC_LATCH` snapshots the live clock, and the seed is built by EOR-ing
    `RTC_SEC`, `RTC_MIN`, `RTC_HOUR` and the two FAT-time bytes into
    `RNG_STATE_LO` and `RNG_STATE_HI`
  - if both state bytes come out zero, `RNG_STATE_LO` is forced to `$01`, because an
    all-zero LFSR state is forbidden
  - `PAGE_ENABLE` is set, and `CMD_LINE_COUNT`, `PAGE_SUSPEND` and `PAGE_ABORT_FLAG`
    are cleared, so the system-wide pager starts on and clean
  - `STZ BEEP_TIMER` - no beep pending
  - `CLI` - enable interrupts
  - `JMP DOS_COLD` - hand off to the MFC-DOS shell
- **Note**: the kernel boots silently. The DOS shell draws the sign-on box, because it
  owns the OS version and free-memory figures. The monitor is a tool launched from DOS
  by `MON`, and it returns through `DOS_WARM`

---
