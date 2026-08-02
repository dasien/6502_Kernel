# BOARD — the MFC 6502 as a single-board computer

MFC is software, but it is built like hardware: discrete chips with their own
registers, wired to a shared bus, decoded by address. This is that machine drawn
the way its schematic would look if you could hold it.

Nothing here is an analogy invented for the diagram — every chip is a C++ class
with its own file, every address range is a real decode in `Memory::read`/`write`,
and every interrupt line is a real call into `CPU6502`.

## Board layout

```
                                       ┌──────────────┐
                                       │  RESET CKT   │  power-on · Ctrl+R
                                       └──────┬───────┘
                                              │ RES
       IRQ ── PIA 60 Hz timer ───────────┐    │
       NMI ── STOP key ──────────────┐   │    │
                                     v   v    v
                             ┌───────────────────────┐
                             │      WDC 65C02        │
                             │    cycle-stepped      │
                             └───────────┬───────────┘
                                         │  A0-A15 · D0-D7 · R/W
  ╔══════════════════════════════════════╧═══════════════════════════════╗
  ║                            SYSTEM  BUS                               ║
  ╚═══╤═══════╤═══════╤═══════╤═══════╤═══════╤═══════╤═══════╤══════════╝
      │       │       │       │       │       │       │       │
   ┌──┴──┐ ┌──┴──┐ ┌──┴──┐ ┌──┴──┐ ┌──┴──┐ ┌──┴──┐ ┌──┴──┐ ┌──┴───┐
   │ PIA │ │ BLK │ │ACIA │ │ VIC │ │ SID │ │ RTC │ │ RAM │ │ ROM  │
   │$FE00│ │$FE24│ │$FE29│ │$FE2D│ │$FE38│ │$FE55│ │ 64K │ │ x5   │
   └──┬──┘ └──┬──┘ └──┬──┘ └──┬──┘ └──┬──┘ └──┬──┘ └─────┘ └──────┘
      │       │       │       │       │       │
      v       v       v       v       v       v
   keyboard  disk    modem  display  audio   host
   + host    image    TCP    80x25    sink   clock
   files    (FAT16)  (BBS)  char+attr

   ── each chip is a C++ class; the row below is its host-side backing ──
```

## Address decode

The CPU sees one flat 64 KB space. `Memory` is the address decoder: it tests each
peripheral's range in turn and forwards the cycle, exactly like the 74-series glue
logic that would sit between the CPU and the chips on a real board.

```
$0000 ┌────────────────────────────────────────────────┐
      │ zero page / stack / system vars                │  RAM
$0400 ├────────────────────────────────────────────────┤
      │ T:/Z: snapshot, assembler symbol table         │  RAM
$0800 ├════════════════════════════════════════════════┤
      │                                                │
      │            USER RAM  (32 KB)                   │  RAM
      │      programs load and run at $0800            │
      │                                                │
$8800 ├════════════════════════════════════════════════┤
      │  MFC-DOS ROM (10 KB)    ABI at $AF00           │  always mapped
$B000 ├════════════════════════════════════════════════┤
      │  MODULE WINDOW (16 KB)   ◄── MODULE_BANK $FE23 │  banked
      │    bank 0 = RAM                                │
      │    bank 1 = BASIC   bank 3 = FORTH             │
      │    bank 4 = MONITOR (+ assembler)              │
$F000 ├════════════════════════════════════════════════┤
      │  KERNEL BIOS ROM (4 KB)                        │
      │    $FE00-$FEFF  I/O page  ── decoded below     │
      │    $FF00        ABI jump table (22 entries)    │
      │    $FFFA        NMI / RESET / IRQ vectors      │
$FFFF └────────────────────────────────────────────────┘
```

Only one bank is mapped at a time, which is why the monitor cannot show you
`$B000-$EFFF`: it is the thing living there.

## The I/O page (`$FE00-$FEFF`)

One 256-byte page holds every chip's registers. It is carved out of the kernel ROM
window and reserved by the `IORESV` linker segment, so kernel code can never grow
into it by accident.

The decode is contiguous and gapless from `$FE00` to `$FE60`, each chip claiming
one span (the ranges below are the `is*Address()` predicates in each class, not a
paraphrase):

| Range | Chip | Class | Registers |
|---|---|---|---|
| `$FE00-$FE22` | **PIA** | `PIA` | keyboard data/status (`$FE00-$FE02`), interval-timer IRQ acknowledge (`$FE0E`), host file I/O — command, status, address, 12-byte name (`$FE10-$FE21`) |
| `$FE23` | *(decoder)* | `Memory` | `MODULE_BANK` — selects the `$B000` window. Not a chip: `Memory` answers this one itself, before any peripheral is consulted |
| `$FE24-$FE28` | **BLK** | `BlockDevice` | LBA, command, status, 512-byte sector port |
| `$FE29-$FE2C` | **ACIA** | `Acia` | serial data, status, command, control |
| `$FE2D-$FE37` | **VIC** | `VIC` | address, char, colour, attribute, command, cursor, scroll region |
| `$FE38-$FE54` | **SID** | `Sid` | 3 voices, filter, master volume (29 registers) |
| `$FE55-$FE60` | **RTC** | `Rtc` | latch, s/m/h/d/m/y, FAT timestamp |

The RTC reaches `$FE60` because the FAT date registers were appended after the
range was first written down; the other docs quoted `$FE5E` until this diagram was
drawn against the `is*Address()` predicates and turned up the difference.
`$FE61-$FEFF` is unclaimed — that is where the next chip goes.

## Interrupts

Two lines into the CPU, both real:

- **IRQ** — level-sensitive, asserted by the PIA's ~60 Hz interval timer. The
  handler must acknowledge at `$FE0E` or it re-fires immediately. Drives BASIC's
  `ON IRQ` and the jiffy counter behind `K_GET_JIFFIES` (`$FF39`).
- **NMI** — edge-triggered, raised by the host STOP key. Breaks into the monitor
  from anywhere. Because the handler is in always-mapped kernel ROM it re-maps the
  monitor bank on the way in, so a program that scribbles on `$FE23` cannot lock
  you out.

RESET is its own circuit: it vectors through `$FFFC`, clears a latched NMI, and
zeroes the module window so bank 0 boots as clean scratch.

## Where the chips meet the host

The right-hand chips are only half the story — each has a host-side backing that
a real board would have as a physical connector:

| Chip | Backed by | Stands in for |
|---|---|---|
| **BLK** | `disk.img`, a FAT16 image | an IDE/CF card |
| **PIA** file I/O | host open/save dialog | a parallel port |
| **ACIA** | `Modem` → TCP socket | an RS-232 port and a Hayes modem |
| **SID** | `SidAudio` → `QAudioSink` | the audio jack |
| **VIC** | `DisplayWidget` | the video connector |
| **PIA** keyboard | Qt key events | the keyboard connector |

The screen is worth calling out: **it is not in the 64K map.** There is no frame
buffer to poke. The VIC owns an 80×25 character-plus-attribute plane and the CPU
reaches it only through the register port at `$FE2D` — set an address, write a
character, the index auto-increments. That is why `$0400-$07FF`, which on a
Commodore 64 would be screen RAM, is free for the monitor and assembler to use.

## Reading the code

| Block | Source |
|---|---|
| CPU | `src/computer/CPU6502.cpp` |
| Address decode / bus | `src/computer/Memory.cpp` |
| Chips | `src/computer/{PIA,BlockDevice,Acia,VIC,Sid,Rtc}.cpp` |
| Reset / clock | `src/computer/{ResetCircuit,TimingCircuit}.cpp` |
| Board assembly | `src/computer/Computer6502.cpp` — the constructor is the wiring |
| Host bridges | `src/computer/{Modem,SidAudio}.cpp`, `src/ui/DisplayWidget.cpp` |

`Computer6502`'s constructor is the closest thing to a netlist: it hands each
peripheral to `Memory`, gives the PIA a pointer to the CPU so it can drive IRQ,
and loads the ROM images into their regions.
