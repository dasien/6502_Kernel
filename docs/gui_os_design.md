# An optional graphical OS for MFC: design

Research spike, written from [briefs/gui-os-research.md](briefs/gui-os-research.md). This is
**part 1 of 2**. It decides how the GUI is launched and left, which display model it
draws on, and how it gets a pointer. Questions 3 to 7 of the brief (what to take from GEOS,
the kernel ABI, filesystem integration, the application model and phasing) are part 2 and
are listed at the end. Nothing here is implemented, and no prototype was built.

The reference is [mist64/geos](https://github.com/mist64/geos), the reconstructed source of
GEOS 2.0. Nothing from it is adapted yet, so there is no credit or `vendor/geos/` change in
this part.

Conventions. Any figure that is not measured is marked **estimate** and shows its
arithmetic. Any address, register or entry point that does not exist today is marked
**proposed**. Everything unmarked is in [architecture.md](architecture.md),
[board.md](board.md) or [video_design.md](video_design.md).

## Summary of decisions

| Question | Decision | Rejected |
|---|---|---|
| Launch | A DOS command `DESK`, a ~20-byte stub program on disk that maps bank 2 and calls the GUI as a subroutine. `DESK` as the last line of `SYSTEM/STARTUP.CFG` starts it at boot | A `MODULE_DIR` entry left by `K_RETURN_MODULE`; a GUI that lives entirely in user RAM |
| Placement | Code in module bank 2. State and the stub in the `$0400-$07FF` scratch page. Overflow and resources on disk | All-RAM `.PRG`; state in bank 0 (not visible while bank 2 is mapped) |
| Display | Glyph-tiled framebuffer: 640x400, 1 bit a pixel, two colours per 8x16 cell, built from nine font sets switched by raster band | Character-cell GUI; new VIC bitmap mode (kept as a drop-in upgrade) |
| Pointer | A chipset mouse device (7 registers, **proposed** `$FED5-$FEDB`), drawn as sprite 16 | Keyboard-only pointer on `$FE0F` (kept as a fallback source) |

## "Optional": launch and exit

### What "optional" means here

The machine boots exactly as it does today: RESET, the boot report, the DOS, the
`SYSTEM/STARTUP.CFG` runner, the sign-on, the `]` prompt. No kernel or DOS routine changes to
make the GUI exist. The GUI is a program the shell launches, as `WIN` was under MS-DOS.

### The command

The command is **`DESK`** (the desktop). It is short like `MON` and `EDIT`, it names the thing
rather than a vendor's product, and it leaves room for later `DESK` arguments. Typing it at
the `]` prompt starts the GUI. Quitting from the desktop's menu returns to the `]` prompt.

### How it is built: a stub and a module

`DESK` is a disk file, `DESK.PRG`, of about 20 bytes, loaded by the DOS like any program
(2-byte load-address header, entry at the start; see `dos_internals.md`, "Program file
format"). It does three things:

```
        LDA #2
        STA MODULE_BANK     ; map bank 2 into $B000-$EFFF
        JSR $B000           ; the GUI's entry; it returns with RTS
        STZ MODULE_BANK     ; bank 0 again, as the DOS expects
        RTS                 ; back to the DOS, as any launched program does
```

The GUI proper is a 16 KB ROM module installed as bank 2. It is built and installed the way
the monitor is (`monitor.rom` into bank 4): a new `gui.rom`, and one more `installBank` line
in the host (**proposed**; the host change is `src/computer/Computer6502.cpp`, done in the
implementation work). Bank 2 is free in `architecture.md`, and it is the one that was the
assembler before the assembler folded into the monitor.

Why a stub rather than registering the module in `MODULE_DIR` like BASIC, FORTH and the
monitor:

- **A module returns through `K_RETURN_MODULE` (`$FF12`), which does not return to its
  caller.** It unmaps the bank, resets the stack and re-enters the shell at the warm entry.
  A disk program returns by `RTS`, to wherever the DOS called it from. That difference
  matters for `SYSTEM/STARTUP.CFG` (below).
- The DOS resolves a name as built-in command, then `MODULE_DIR` module, then disk file. A
  stub on disk needs no `MODULE_DIR` record, so the kernel ROM, which has little free
  space, gains nothing.
- The stub is the only part that must be on disk. The code that is large, and
  that must not be lost to a reload, is in ROM.

`DESK` therefore appears in `CATALOG` and can be copied or replaced like any program. `&DESK`
is not needed. `BANKS` lists only `MODULE_DIR`, so it will not show bank 2. The boot report
reads identity blocks from banks 1 to 255, so a `ROM_ID` block in `gui.rom` adds one line,
"GUI ROM installed as module bank 2", to the report. That is the same line every other bank
prints, and no boot step changes.

### Quitting and "DOS state intact"

The GUI is entered with `JSR` from the stub, which the DOS entered with `JSR`. Quitting runs,
in order:

1. `K_RASTER_IRQ` (`$FF45`) with 0, so no raster handler is left calling into a bank that is
   about to go.
2. `kCmdFontReset` (`$0A`) then `kCmdClear` (`$01`) through `VREG_CMD`. The first reloads
   CP437 into every font set, because the display model below overwrites nine of them; the
   second selects the ROM font, drops the sprites and resets the scroll region and fine scroll.
3. `RTS` to the stub, which unmaps bank 2 and `RTS`es to the DOS.

DOS state is intact because the GUI never writes where the DOS keeps it. The DOS's variables
are in page 3 and up (`$0300` onward) and in the DOS ROM's own workspace; the current drawer
(`OPEN`) is one of them. The GUI's state is in `$0400-$07FF` (below), its code is in
bank 2, and it prints nothing through `K_PRINT_CHAR`, so the pager and cursor are untouched. The
stack depth returns to what it was because the GUI is a subroutine. After the clear the
prompt is redrawn at the top of a blank screen, which is what `K_RETURN_MODULE` and `CLS` do
today.

### Starting it at boot

Add one line to `SYSTEM/STARTUP.CFG`, as the last line:

```
# SYSTEM/STARTUP.CFG
OPEN GAMES
DESK
```

`_DOS_STARTUP` runs each line through the shell's own interpreter, so `DESK` launches the stub
like a typed command. The stub's `RTS` is the reason the design uses one. The config runner
turns paging off for the whole file and restores it after the last line. A `MODULE_DIR`
module would leave through `K_RETURN_MODULE`, which re-enters the shell directly: the
remaining lines would never run, the pager would stay off, and the sign-on box would be
skipped. A disk program returns to the runner's loop, which is how it already treats
`OPEN` or a game. Quitting the GUI after a boot launch therefore finishes the config,
prints the sign-on and shows the `]` prompt, exactly as a boot without the GUI would.

Put `DESK` last because a line after it does not run until the user quits. The existing
escape hatch covers a broken desktop: ESC pressed during boot skips the file, and the STOP
key (NMI) breaks into the monitor. DOS itself never starts the GUI; with no `DESK` line, nothing
changes.

### Placement: bank 2 confirmed, with a caveat about state

Bank 2 fits and is the right home, with two facts to design around.

**Banks 1 to 255 are ROM, and bank 0 is the only RAM.** `Memory` ignores writes to a mapped
non-zero bank. So the GUI cannot keep variables in the window, and it cannot use bank 0 as
scratch, because bank 0 is not visible while bank 2 is mapped. The module is code and
constants only. Writable state goes in user RAM, which is always visible.

The state goes in **`$0400-$07FF`**. `architecture.md` calls it scratch for any program
that uses neither the monitor's `T:`/`Z:` snapshot nor the assembler. The GUI uses neither.
That is 1 KB, minus the stub, which can sit at the top of it (**proposed** load address
`$07E0`, so the stub survives a program loaded at `$0800`; the GUI's own state then has
`$0400-$07DF`, 992 bytes). It holds the pointer, the event queue, menu and window records
and dirty-rectangle list. A zero-page budget of about 32 bytes fits the free gap at
`$3A-$5A` that `architecture.md` lists; part 2 pins that down.

Two consequences. A **STOP** key press maps the monitor over the window, and `Q` returns to
the DOS prompt, not the desktop: the GUI is dropped, and that is acceptable for a debugger
break. The NMI handler should also disable the raster interrupt (**proposed**, a few bytes in
the kernel), because the display model installs a raster handler, and that handler is in
the window the NMI path unmaps. And a plain `.PRG` that the desktop launches loads at `$0800`
and may use `$0400-$07FF` as scratch, so the desktop rebuilds its state from the DOS and the
disk when such a program returns. Only GUI-aware applications (question 6) promise to leave
that page alone.

**16 KB is the budget.** Event loop, menus, dialogs, graphics primitives and one text
renderer are the core of such a toolkit. I expect them in 10 to 14 KB of 65C02 code
(**estimate**, from the size class of comparable 8-bit toolkits; not measured, and part 2
gives per-part sizes). The remainder, 2 to 6 KB, is little for icons and fonts, so those
live on disk and load into user RAM or video RAM at start. If the code alone exceeds 16 KB,
a second ROM bank (any bank above 4 is unused; **proposed** bank 5) holds the desktop and
file manager, reached through a trampoline in the `$0400-$07FF` page. That is a
page-sized change, not a redesign.

### Alternatives considered

| Option | Result |
|---|---|
| **`.PRG` in user RAM** | Rejected as the home of the code. A 10 to 14 KB resident GUI leaves 18 to 22 KB of the 32 KB at `$0800-$87FF` for applications (**estimate**: 32 KB less the code), and every application loads at `$0800`, so the GUI would have to sit high and crowd the C stack at the top of user RAM. Launching an application would overwrite or evict it. It also needs the disk and a full load to start. It is simpler to build, which is its only advantage. |
| **Module only, via `MODULE_DIR`** | Rejected. Works for typed `DESK`, but exits through `K_RETURN_MODULE`, which breaks `STARTUP.CFG` (above) and needs a kernel ROM record. |
| **Mix: ROM code, RAM state, disk resources** (recommended) | The code costs no user RAM, the stub makes launch and exit ordinary DOS behaviour, and the only kernel change is the optional NMI line. |

## Question 1: Display model

### The constraints

The VIC is a text chip. The cell planes and the 16 font sets live inside it and are reached
through the register port at `$FE2D-$FE37` and `$FE62-$FE64`, so the guest pays one port write
per byte and pays no address space (`video_design.md`, "Architecture"). A cell is 8x16
nominal pixels, and the screen is 80x25 cells, which is 640x400 nominal pixels. The host
scales that, so a pixel is the same size in both directions.

All redraw times below use these assumptions, and each is **estimate**:

- The clock is 4 MHz and the frame is 60 Hz, so one frame is 4,000,000 / 60 = 66,667 cycles.
- A port access costs the same as a RAM access. `board.md` says there is no bus contention and
  the host models no device delay.
- A copy loop from RAM to a port is `LDA (zp),Y` (5) + `STA port` (4) + `INY` (2) + `BNE` (3) =
  **14 cycles a byte**. A fill with one value, unrolled as repeated `STA port`, is **4 cycles
  a byte**.

Nothing was measured. The cycle counts are exact for a loop of that shape, and the first
implementation slice (part 2) should time a full region redraw on the emulated machine to confirm
the port costs the same as RAM.

### The three options

**A. Glyph-tiled framebuffer.** Every cell of a region gets its own glyph code, and drawing
means writing that glyph's 16 bytes in font RAM through `VREG_FONT_DATA`. One 256-glyph font
is 4,096 bytes and covers 16x16 cells = 128x256 pixels. There are 16 sets, and `kCmdFontSet`
can change the live set mid-frame as a raster split (`video_design.md`, "The raster": the
font is one of the things that splits). A cell row is 16 pixel lines, so a split at a
multiple of 16 lines changes the font for a whole row. With 80 cells in a row, a band of 3
rows holds 240 distinct glyphs, which fits one set. Nine bands (8 of 3 rows, 1 of 1 row)
cover all 25 rows, so the **whole screen is a 640x400 bitmap using nine font sets**.

Colour comes from the cell's attribute byte: a 3-bit foreground (plus bright) and a 3-bit
background per 8x16 cell, which is two colours in each cell, set through `VREG_COLOR` or the
`VREG_ATTR` latch. Pixels within a cell cannot differ in colour, like the ZX Spectrum's
attribute clash but on a coarser 8x16 grid.

**B. Character-cell GUI.** Windows, menus and dialogs from CP437 box-drawing characters and
the soft font. There are no pixel graphics. A cell is the smallest thing that can be
addressed: 80x25 positions. Each cell takes any of the 256 glyphs in the live set and its own
colours. Sprites supply the pointer and icons.

**C. A new VIC bitmap mode** (**proposed**). A chip-side mode bit makes the renderer draw a
linear bitmap instead of glyphs. The baseline is 640x400 at 1 bit a pixel in 32,000 bytes of
chip RAM, with colour from the existing attribute plane per cell. A 4-bit variant at 320x200
(16 colours a pixel) uses the same 32,000 bytes and the same time through the port.

### Comparison

| | A. Glyph-tiled | B. Character-cell | C. New bitmap mode |
|---|---|---|---|
| Resolution | 640x400 using nine bands of font sets (128x256 with a single set) | 80x25 cells; the pixel grid is not addressable | 640x400 (or 320x200 at 4 bpp) |
| Colours | 2 per 8x16 cell (3-bit fg + bright, 3-bit bg) | 2 per cell | 2 per cell at 1 bpp; 16 per pixel at 4 bpp |
| Memory in the 64K map | 0 | 0 | 0 |
| Memory in the chip | 9 x 4,096 = 36,864 B of the 65,536 B font RAM; 7 sets stay free | 4,000 B (cell plane and colour plane, already there) | 32,000 B of new chip RAM |
| Full-screen redraw | 32,000 B x 14 = 448,000 cycles = 112 ms = 6.7 frames (copy); 32,000 x 4 = 128,000 = 32 ms = 1.9 frames (solid fill) | 2,000 cells x 14 = 28,000 cycles = 7.0 ms = 0.42 frame; 14 ms = 0.84 frame if every cell also takes its own attribute | Same bytes, same time as A: 112 ms copy, 32 ms fill. Less with a chip-side fill or blit, which is extra cost |
| One 4 KB region (a 128x256 window) | 4,096 x 14 = 57,344 cycles = 14.3 ms = 0.86 frame | 40x12 cells = 480 x 14 = 6,720 cycles = 1.7 ms | Same as A |
| Text, byte-aligned (monospace) | 16 bytes a glyph x 14 = 224 cycles = 56 us; an 80-char line is 4.5 ms | 14 cycles a character; an 80-char line is 1,120 cycles = 0.28 ms | As A |
| Text, proportional | About 60 cycles a pixel row (shift, mask, read-modify-write across two bytes) x 16 rows = 960 cycles = 0.24 ms a character; a 40-char line is 9.6 ms | Not possible | As A |
| Icons, lines, patterns | Yes, pixel-exact | Only as 2x2 glyph tiles or sprites | Yes, pixel-exact |
| Chip work (C++) | None | None | New registers, renderer, tests (below) |
| Pointer sprite | Composited above the planes, no save-under | Same | Same, if the renderer keeps sprites on top |

The 56 us figure is 224 / 4,000,000. The 14 ms figure for B with attributes is two passes of
2,000 cells (28,000 cycles each, because `VREG_CHAR` takes the colour from the latch and the
per-cell colour needs a second stream through `VREG_COLOR` with the address set again).
Because all of these are loop-shape arithmetic, they are lower bounds: a real drawing routine
adds address setup, clipping and the branches around the loop. A typical dialog (320x160
pixels, 6,400 bytes) is 6,400 x 14 = 89,600 cycles = 22 ms = 1.3 frames, and a menu
(160x112, 2,240 bytes) is 31,360 cycles = 7.8 ms.

### What each option costs

**A costs no chip work and pays in CPU and in discipline.** The nine-band split uses
machinery that exists and has tests (`tests/test_vic_raster.cpp`, and the font set switching
in `tests/test_vic_softfont.cpp`), driven by `K_RASTER_IRQ` (`$FF45`). A band change is one
IRQ per band. Each costs about 100 cycles: 7 to enter, about 30 in the kernel's check and
register save, 6 for the `JSR`, 20 in the handler to set the next compare line and the next set, 6 for `RTS`, about
30 to restore and `RTI`. Nine of them are 900 cycles a frame, which is 1.4% of 66,667
(**estimate**). The rules the raster section imposes become GUI rules. The GUI never
writes `$FECD` to present, because a presenting program is drawn from its live settings and
its bands are ignored; the cost is that a frame is shown at the next boundary, at most one
frame (16.7 ms) after it is finished. The hardware cursor is hidden through
`VREG_CURSOR_HI`. A drawing routine addresses a pixel as (set, glyph, row), not (x, y),
and that mapping lives in one routine so the rest of the GUI sees a 640x400 plane.
The mapping is also what makes C a cheap upgrade later. Setting up the cell plane (glyph
codes and colours for 2,000 cells) is done once at start: 2,000 x 14 = 28,000 cycles = 7 ms.

Drawing is read-modify-write through the port, since there is no 32,000-byte copy of the
screen in RAM to compose in (user RAM is 32 KB in total). A compositing buffer is a region
buffer the size of the window being drawn, in tile order, in user RAM. That is the real
memory cost of A and of C, and it is the same for both.

**B costs nothing and gives up the point of the exercise.** It is the cheapest and fastest
option, and it is what the MFC shell already is. But it has no proportional fonts, no icons
that are not tiles, no lines at an angle, and no dialogs that are anything but a grid of
80x25. The brief names proportional fonts, menus and a file manager with icons as the
target. B delivers menus and dialogs, and it does not deliver the rest, so it is a text-mode
user interface (Turbo Vision, the DOS Shell) rather than a GEOS. It is the right fallback
if A fails its first measurement.

**C costs a chipset change, and A already gives the same picture.** The cost, concretely
(**estimates**):

- *VIC registers.* A mode/control register, a bitmap byte index (low and high, since 32,000
  needs 15 bits) and an auto-incrementing data port, which is **4 bytes**. A clear-bitmap
  command goes through `VREG_CMD` at no register cost. A chip-side fill or blit (which is what
  would make C faster than A) is 8 to 10 more bytes for source, destination, size and a go.
- *I/O budget.* `$FED5-$FEFF` is 43 bytes. The minimum C takes 4 of them, leaving 39. With a
  blitter it takes about 14, leaving about 29. The mouse (below) takes 7 of the same 43 whichever
  option is chosen, so C with a blitter and a mouse leaves about 22.
- *Renderer.* `DisplayWidget` draws the bitmap with per-cell colours in place of glyphs when
  the mode bit is set, and has to agree with the raster-band logic (a band may carry the mode
  or not) and with sprites, which composite on top. About 150 to 250 lines of C++ in
  `VIC.h`, `VIC.cpp` and `DisplayWidget.cpp`. `video_design.md` notes that "renderer geometry
  is not unit-testable here and is verified by eye", so the renderer is the part with no automated
  test.
- *Tests.* A new `tests/test_vic_bitmap.cpp` in the style of `test_vic_softfont.cpp`: the
  addresses pinned, the index auto-incrementing and wrapping at 32,000, the data
  round trip, the mode switching off to text, `kCmdClear` turning it off, and the sprites
  and font untouched. About 100 to 150 lines. Plus `board.md` and `architecture.md` tables and the
  memory-layout validator.

What C buys over A is **linear addressing** (a horizontal line is contiguous bytes, not a
stride of 16), no raster IRQ, and 7 font sets that the bitmap leaves untouched, where A holds
nine sets. Redraw speed is the same unless the blitter is built. Those gains do not
justify a chip change before anything has been drawn.

### Recommendation

**Build the GUI on the glyph-tiled framebuffer (A): a 640x400, 1-bit plane with two colours
per 8x16 cell, held in nine font sets switched by raster band.** It gives the pixels the brief
wants (lines, icons, proportional text) from hardware that exists and is tested, at no I/O
budget and no C++ change. Every drawing routine goes through one "(x, y) to (set, glyph, row)"
mapping, so if the first measurement shows the strided addressing or the raster bands to be a
real problem, C replaces that one routine and the rest of the GUI does not change.

Rejected: **B** because it is a text UI and cannot show proportional type or icons, which is
the goal; it remains the fallback. **C** because it spends the last scarce resource, I/O page
bytes and a renderer, to get the picture A already produces, and it can be added later behind
the same mapping routine if the measurement asks for it.

## Question 2: Pointer and input

### What exists

A keyboard reaches the guest two ways. `PIA_DATA` is a queue of typed bytes; the arrow keys
arrive as ESC-prefixed sequences (`ESC [ A` to `ESC [ D`), and there is no key-up. `KEY_STATE`
at `$FE0F` is the live held-key bitmask: bit 0 Up, 1 Down, 2 Left, 3 Right, 4 Fire (Space),
5 Button 2 (Left Shift), updated by the host on every press and release. It reads 0 in the
console build and the headless test harness. The host already has mouse handlers in
`DisplayWidget` (`mousePressEvent`, `mouseMoveEvent`, `mouseReleaseEvent`), used only to select
text for copying. There is no mouse device.

The sprite block has 17 sprites at `$FE65-$FECA`, positioned in nominal pixels 0 to 639 by 0
to 399, with bitmap patterns (16x16, 4 bits a pixel, palette slots 1 to 15, slot 0
transparent) from the pattern RAM at `$FECE-$FED0`. The renderer draws them in index order
over the planes (`DisplayWidget::drawSprites` loops 0 to 16), so **sprite 16 is topmost**.
Neither the scroll region, the fine scroll nor the raster bands move them.

### The pointer is a sprite

Whichever device supplies the position, the pointer is a bitmap sprite, sprite 16
(**proposed** convention). Reasons:

- It composites over the glyph-tiled plane, so it needs no save-under, no restore and no
  redraw when it moves. In GEOS the pointer was a hardware sprite as well, for the same reason.
- One 16x16 arrow costs one pattern slot, which is 128 bytes: 128 x 14 = 1,792 cycles to
  upload once (**estimate**), which is under 3% of a frame.
- Moving it is writes to the position bytes only. Offset 1 and offset 3 also carry size,
  magnify and enable bits, which the GUI holds constant: four stores of
  about 4 cycles plus the operand setup, around 30 cycles in all (**estimate**), so every
  frame is affordable.
- A second pattern slot per cursor shape (arrow, text beam, busy) is switched by changing the
  one glyph register, as `video_design.md` describes for animation.
- The sprite's pixel grid is the framebuffer's: 640x400. The pointer's hot spot maps to a
  framebuffer pixel with no scaling.

The cost is that the pointer is not part of the plane, so a screen capture of the plane does not
include it, and the GUI has 16 of the 17 sprites left for other overlays (drag outlines,
icons being dragged).

### Option 1: keyboard pointer

Poll `$FE0F` once per frame with `K_WAIT_FRAME` (`$FF42`). The four direction bits give eight
directions. Fire (Space) is button 1 and Left Shift is button 2.

The speed cannot be analogue, so the pointer needs an acceleration ramp: 1 pixel a frame for the
first few frames, rising to 8. Crossing the screen at the top speed is 640 / 8 = 80 frames =
1.3 s, and at the start it is 640 frames = 10.7 s. A pixel-accurate pick of a small target
means releasing and re-pressing to reset the ramp. It costs no chipset work and has no
host-side code.

Problems:

- **Two buttons only**, and both are keys the typist also uses: Space inserts a space in a text
  field, and Shift is a modifier. The GUI would need a mode to say whether Space is a click or
  a character.
- **ESC is not an action.** The arrow sequences in the queue begin with a bare ESC, and held-key
  buffer drops can leave a lone ESC. A GUI that treats a lone ESC as "cancel" would
  mis-cancel. (The same rule is already followed by the full-screen programs.)
- **No drag with a modifier**, because Space and Shift are the only buttons.
- **Not testable by the existing suites without a host path**: the register reads 0 in the
  console build and the headless harness. Only a test that calls `PIA::setKeyState` can drive
  it, and then it does not exercise the host mapping.

### Option 2: a chipset mouse device

A new device, `Mouse`, a chip in the board's own style. The host reports the pointer's
**absolute** position in nominal pixels, so no relative deltas, no acceleration and no pointer
grab are needed: the sprite sits where the host's pointer is.

**Registers** (**proposed**, `$FED5-$FEDB`, 7 bytes of the 43 free):

| Address | Name | Contents |
|---|---|---|
| `$FED5` | `MOUSE_X_LO` | X, bits 7-0 (0 to 639). Reading it latches `MOUSE_X_HI` and `MOUSE_Y` so the three read as a pair |
| `$FED6` | `MOUSE_X_HI` | X bit 8 and bit 9, from the latch |
| `$FED7` | `MOUSE_Y_LO` | Y, bits 7-0 (0 to 399), from the latch |
| `$FED8` | `MOUSE_Y_HI` | Y bit 8, from the latch |
| `$FED9` | `MOUSE_BTN` | Bit 0 left, bit 1 right, bit 2 middle, held right now |
| `$FEDA` | `MOUSE_WHEEL` | A wrapping counter of wheel steps, not cleared by reading (the frame counter's rule) |
| `$FEDB` | `MOUSE_CTL` | Bit 0: capture. Set by the guest to say the guest owns the mouse |

The low-byte-latches-the-rest read follows the RTC and the raster; the counter that is not
cleared by reading follows `$FECD`. Cost is 7 of 43 bytes, leaving 36.

**Host side.** `DisplayWidget` already sees mouse events. It would call
`setMouseTracking(true)`, convert each event's position by the current zoom into nominal
pixels (the widget knows its character size and zoom, and `board.md` fixes the 640x400 grid),
clamp to 0..639 and 0..399, and hand it to the device, as `DisplayWidget::updateKeyState`
hands `PIA::setKeyState` the held keys. While `MOUSE_CTL` capture is set the widget stops
using left-drag for text selection and hides the host cursor (`Qt::BlankCursor`), because the
sprite is now the cursor. When capture is clear, which is every other program, selection
works as it does today. In the console build and the headless harness the registers read 0
and the GUI falls back to the keyboard.

**Estimated cost:** about 150 lines of C++ for the device and 40 in `DisplayWidget`, plus a new
`tests/test_mouse.cpp` in the style of `test_pia_keystate.cpp` (addresses pinned and not
colliding with the neighbours, the latch giving a coherent pair, clamping, buttons, the wheel
counter wrapping, capture). Plus the `board.md` decode table and `architecture.md`
register table.

**Why testing is better here.** The device is driven by a single call, so a test (and the
GUI's own integration tests, once they exist) can place the pointer at a pixel and press a
button without a window. The keyboard path cannot say "click at (320, 200)".

### Recommendation

**Add the chipset mouse (option 2) and draw the pointer as sprite 16.** A windowed desktop
with menus, drag, double-click and text fields needs a pointing device with a real position
and at least two buttons that are not typing keys, and the host already has one under the
user's hand. Seven I/O bytes, one small device class and one test file buy that, and they
make the whole GUI testable without a window.

The keyboard is not rejected as a *source*. The GUI holds one pointer model (position and
buttons) with two feeders, the mouse registers when `MOUSE_CTL` capture reads back, and a
keyboard ramp over `$FE0F` otherwise. That is about 40 lines and is the way the GUI works in
the console build and under the headless harness. As the *primary* design the keyboard
is rejected for the reasons above: no absolute position, two buttons that collide with typing,
10 s to cross the screen from rest.

## Parts of the brief still to write (part 2)

Questions 3 to 7 are not answered here. They need the decisions above and are the second
bead.

- **3. Architecture from GEOS**: what to adopt, adapt or drop; every adapted part names its
  mist64/geos file.
- **4. Kernel ABI**: existing `$FF00` routines used, each proposed new entry with inputs,
  outputs and ROM cost. This part proposes three things that part 2 must cost: a pointer-state
  entry, a rectangle and glyph-region blit pair, and the NMI line that disables the raster
  interrupt.
- **5. Filesystem integration**: the desktop over FAT16, launching a `.PRG` and returning, and
  the `$0400-$07FF` state rebuild described above.
- **6. Application model**: what a GUI application is, and a minimal "hello window".
- **7. Phasing**: ordered, demonstrable slices. The first should be the measurement: time a
  full region redraw through `VREG_FONT_DATA` and the nine-band split on the emulated machine, and
  compare it with the estimates in the Question 1 table.
