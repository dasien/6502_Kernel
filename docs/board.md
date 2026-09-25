# The MFC 6502 as a single-board computer

MFC is software, but it is built like hardware. Every chip has its own registers,
the chips are wired to a shared bus, and each one is selected by address. This
document draws that machine the way its schematic would look if you could hold it.

None of this is an analogy invented for the diagram. Every chip is a C++ class with
its own file, every address range is a real decode in `Memory::read` and
`Memory::write`, and every interrupt line is a real call into `CPU6502`.

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

The CPU sees one flat 64 KB space. `Memory` acts as the address decoder, testing
each peripheral's range in turn and forwarding the cycle to whichever one claims it.
That is the job the 74-series glue logic would do between the CPU and the chips on a
real board.

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
      │    $FF00        ABI jump table (23 entries)    │
      │    $FFFA        NMI / RESET / IRQ vectors      │
$FFFF └────────────────────────────────────────────────┘
```

Only one bank is mapped at a time. That is why the monitor cannot show you
`$B000-$EFFF`, because the monitor is itself the thing living there.

## The I/O page (`$FE00-$FEFF`)

One 256-byte page holds every chip's registers. It is carved out of the kernel ROM
window and reserved by the `IORESV` linker segment, so kernel code can never grow
into it by accident.

The decode runs from `$FE00` to `$FECD`, and each chip claims one span. The ranges in
the table below are taken from the `is*Address()` predicate in each class rather than
paraphrased from it. The decode is contiguous and gapless with a single exception.
The VIC answers two separate ranges, because the soft-font port and the sprite block
were both added after the SID and the RTC had taken the addresses next to its first
range. `isVideoRegAddress()` is therefore three tests rather than one.

| Range | Chip | Class | Registers |
|---|---|---|---|
| `$FE00-$FE22` | PIA | `PIA` | Keyboard data and status at `$FE00-$FE02`, interval-timer IRQ acknowledge at `$FE0E`, live held-key state at `$FE0F`, and host file I/O command, status, address and 12-byte name at `$FE10-$FE21` |
| `$FE23` | decoder | `Memory` | `MODULE_BANK`, which selects the `$B000` window. This is not a chip. `Memory` answers it directly, before any peripheral is consulted |
| `$FE24-$FE28` | BLK | `BlockDevice` | LBA, command, status, and a 512-byte sector port |
| `$FE29-$FE2C` | ACIA | `ACIA` | Serial data, status, command, control |
| `$FE2D-$FE37` | VIC | `VIC` | Address, character, colour, attribute, command, cursor, scroll region |
| `$FE38-$FE54` | SID | `SID` | Three voices, filter, and master volume, in 29 registers |
| `$FE55-$FE60` | RTC | `RTC` | Latch, seconds, minutes, hours, day, month, year, and a FAT timestamp |
| `$FE61` | PWR | `PowerSwitch` | The soft power switch |
| `$FE62-$FE64` | VIC | `VIC` | Soft-font index and data port, which is the second of the VIC's two ranges |
| `$FE65-$FECA` | VIC | `VIC` | Seventeen sprite records of six bytes each |
| `$FECB-$FECC` | VIC | `VIC` | Soft palette: byte index, then an auto-incrementing data port |
| `$FECD` | VIC | `VIC` | Frame counter on read; a write presents the finished frame |

The RTC reaches `$FE60` because the FAT date registers sit above the clock
registers proper. The authority for every range here is the chip's own
`is*Address()` predicate.

`$FECE-$FEFF` is unclaimed. That leaves 50 bytes, and it is where the next chip goes.
The size of the sprite block was chosen against that figure rather than against a
theoretical peak. Twenty-five sprites would have fitted but would have left only five
free bytes, so seventeen were taken instead.

This paragraph and the one above it went on claiming that `$FE61-$FEFF` was free for
some time after the sprite and soft-font rows had been added to the table, because the
prose was not updated alongside them. Nothing generates that table, so it agrees with
the code only because somebody checked it.

The PIA's range is not densely packed. `$FE03-$FE0D` is the unused port-B side, and
`$FE0F` was another hole inside a span the decoder already routed. `$FE0F` later
became the key-state port, which is why that feature needed neither a new chip nor any
change to the decoder. `architecture.md` gives the bit layout and explains why an
action game cannot use the keystroke buffer for movement.

## Interrupts

Two interrupt lines run into the CPU, and both of them are real.

IRQ is level-sensitive and is asserted by the PIA's interval timer at roughly 60 Hz.
The handler must acknowledge it at `$FE0E` or it will re-fire immediately. It drives
BASIC's `ON IRQ` and the jiffy counter behind `K_GET_JIFFIES` at `$FF39`.

NMI is edge-triggered and is raised by the host STOP key, which breaks into the
monitor from anywhere. Because the handler lives in always-mapped kernel ROM, it
re-maps the monitor bank on the way in, so a program that scribbles on `$FE23` cannot
lock you out.

RESET has a circuit of its own. It vectors through `$FFFC`, clears a latched NMI, and
zeroes the module window so that bank 0 boots as clean scratch.

## Where the chips meet the host

This section is the authoritative account of where the emulated machine ends and the
host begins. The rule is simple.

- **The machine** is the 6502 software and the chip models in `src/computer/`. All of
  it runs on machine time, counted in CPU cycles. The 60 Hz jiffy and the frame
  boundary come from the cycle count, so a second of machine time holds sixty of
  each whatever the host is doing. A test can run the machine with no window,
  no clock and no network.
- **The host** is everything that touches the real world: the Qt run loop in
  `src/ui/`, the window, the speakers, the wall clock, the network and the Mac's
  files. It decides how many cycles to run, at most 50 ms of catch-up per slice
  (`MainWindow`), and it carries data across at the chips' edges.

The chips are the only doors between the two. Each one exposes registers to the
6502 on one side and has a host-side backing on the other. On a real board that
backing would be a connector with a peripheral plugged into it.

| Chip | Backed by | Stands in for |
|---|---|---|
| BLK | `disk.img`, a FAT16 image | An IDE or CF card |
| ACIA | `Modem`, over a TCP socket | An RS-232 port and a Hayes modem |
| SID | `SidAudio`, over a `QAudioSink` | The audio jack |
| VIC | `DisplayWidget` | The video connector |
| PIA keyboard | Qt key events | The keyboard connector |
| RTC | The host's local time | A battery-backed clock chip |
| PowerSwitch | Closing the window | A soft-power supply |
| PIA file I/O | The host filesystem and its open and save dialogs | Nothing: see below |

### Interfaces from the period, devices without delays

Most of those rows are period hardware. A 6551 on RS-232 driving an external
Hayes modem is how a home computer went online, and only the far end differs, a TCP
connection instead of a phone line. A sector-addressed disk controller is what a SASI
or IDE adapter gave a late 8-bit machine. What the host adds is speed. **No backing
models the time its real counterpart took**, and that is deliberate.

- The ACIA takes a baud rate but does not pace bytes at it. Data moves as fast as
  the 6502 can handle it, not at 300 or 19200 baud.
- A sector read or write completes at once, with no seek or rotation time.
- The RTC reads the host clock at the moment it is latched.

The interfaces are faithful, so the software drives them the way it would drive the
real thing. Only the waiting is gone.

### Keyboard: decoded and polled

There is no keyboard interrupt. `DisplayWidget` turns a Qt key event into an ASCII
byte, and the PIA queues it in a FIFO. The kernel's `K_GET_KEYSTROKE` polls that
queue. Games read the live held-key port at `$FE0F` instead, which the host updates
on every press and release. The machine therefore sees an encoded keyboard with a
buffer, as on an Apple II with a type-ahead queue. It never sees a key matrix, and
there is no scan loop in the jiffy handler.

### The one exception: host file I/O

The PIA's file registers at `$FE10-$FE22` are not an interface to a device. A
program writes a filename and an open command, and a file on the Mac opens. There is
no bus protocol and nothing on the far end to talk to. Emulators call this a **trap**,
and VICE's virtual-device traps do the same for C64 disk access. The nearest real
equivalent would be a serial link to a second computer running a file server. BASIC's
`LOAD`/`SAVE`, the monitor's `L:`, and DOS's `IMPORT`/`EXPORT` all use it. Everything
on `disk.img` goes through the BlockDevice instead.

### The display

The VIC's plane is machine state. Showing it is host work. `MainWindow` repaints
through `Computer6502::takeRepaintDue()`:

- at once when a program presents a finished frame by writing `$FECD`;
- at a frame boundary, if the frame before it was not presented;
- at every boundary, if a whole frame passes without a present.

The host has no display clock of its own. `docs/video_design.md` explains why.

The screen is worth calling out, because it is not in the 64K map at all. There is no
frame buffer to poke. The VIC owns a plane of 80 by 25 characters and attributes, and
the CPU reaches it only through the register port at `$FE2D`. A program sets an
address and writes a character, and the index auto-increments from there. That is why
`$0400-$07FF`, which would be screen RAM on a Commodore 64, is free here for the
monitor and the assembler to use.

The same is true of everything else the chip holds. Its font is RAM rather than a
fixed ROM, and it holds sixteen complete sets of 256 glyphs that a program can switch
between with a single write. Its seventeen sprites are pixel-positioned glyphs drawn
over the cell planes. None of it is addressable, and all of it is reached through
ports.

That makes the VIC a chip in the mould of the TMS9918 or the C128 VDC rather than the
C64 VIC-II, which read the CPU's own RAM and paid for it in bus contention. The trade
here is that every byte of font or sprite data costs a port write. That is why the
font is organised as switchable sets, since a program can upload its variants once and
then change the whole character set with one write.

Two of those features exist for a single reason, which the VIC's header comment
explains at greater length. The scroll quantum is a whole character cell, and a whole
cell of movement reads as a strobe rather than as motion. Fine scroll slides the
scroll region by a pixel count so that the world can move in sub-cell steps. Sprites
are then necessary because that offset moves everything inside the region, so a
screen-fixed object such as a player's craft would sawtooth by a cell on every scroll.
A sprite sits outside the region and so does not. `docs/video_design.md` gives the
full reasoning.

## Reading the code

| Block | Source |
|---|---|
| CPU | `src/computer/CPU6502.cpp` |
| Address decode and bus | `src/computer/Memory.cpp` |
| Chips | `src/computer/{PIA,BlockDevice,ACIA,VIC,SID,RTC}.cpp` |
| Reset and clock | `src/computer/{ResetCircuit,TimingCircuit}.cpp` |
| Board assembly | `src/computer/Computer6502.cpp`, where the constructor is the wiring |
| Host bridges | `src/computer/{Modem,SidAudio}.cpp` and `src/ui/DisplayWidget.cpp` |

`Computer6502`'s constructor is the closest thing the project has to a netlist. It
hands each peripheral to `Memory`, gives the PIA a pointer to the CPU so that it can
drive IRQ, and loads the ROM images into their regions.
