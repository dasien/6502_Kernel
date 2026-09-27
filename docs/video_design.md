# Video — the MFC VIC display chip

MFC has a software video chip with a CRTC and terminal lineage rather than a
home-computer game one. It drives an 80x25 character display in CP437, with a
per-cell colour attribute, from private planes behind a register port. Its
ancestry is visible in the register map, which has a VDC-style address and data
port, a 6845-style hardware cursor, a CGA attribute byte, VT100 double-height
rows and a block-op command engine.

Three features were added on top of that inheritance to make it usable for
real-time games. Those are a redefinable character set, a fine vertical scroll
and sprites. The register-level result is in `architecture.md` and `board.md`,
and the reasoning that led to each is recorded in `../TODO.md`.

## Architecture

The character and colour planes are not in the 6502's address space. They live
inside the chip and are reached through the register port at `$FE2D-$FE37`: set
a cell index with `VREG_ADDR_LO/HI`, then read or write the auto-incrementing
`VREG_CHAR` and `VREG_COLOR` data ports. This is the TMS9918 and VDC model
rather than the VIC-II one, so it costs the guest no address space and there is
no bus contention, at the price of a port write per byte.

`VREG_CHAR` takes a full 8-bit CP437 code point. Everything else about a cell's
appearance is in the attribute plane, latched through `VREG_ATTR`: bit 7
reverse, bit 6 bright, bits 5-3 background and bits 2-0 foreground, over eight
colours. The default is `$02`, green on black.

`VREG_CMD` drives chip-side block operations, so clearing or scrolling costs the
CPU one write rather than a few thousand. A real VIC-II stole around 40 cycles
per badline for this and the VDC made you poll a status bit. Here the work is
free.

| Command | Code | Effect |
|---|---|---|
| `kCmdClear` | `$01` | Fill the screen, reset rows, font, fine scroll and sprites |
| `kCmdScrollUp` | `$02` | Scroll the region up one row, blank the bottom |
| `kCmdScrollDown` | `$03` | Scroll the region down one row, blank the top |
| `kCmdFillRow` | `$04` | Fill the row holding the current cell |
| `kCmdRowSize` | `$05` | Make one row double-height |
| `kCmdRowsNormal` | `$06` | Every row back to 8x16 |
| `kCmdScrollTop` | `$07` | Set the scroll region's top row |
| `kCmdFontRom` | `$08` | Render from the CP437 ROM |
| `kCmdFontRam` | `$09` | Render from font RAM |
| `kCmdFontReset` | `$0A` | Reload CP437 into every set |
| `kCmdFontSet` | `$0B` | Select the live font set |
| `kCmdFineY` | `$0C` | Set the fine vertical scroll offset |

`kCmdRowSize`, `kCmdScrollTop` and `kCmdFineY` all consume the shared command
parameter, so the fill character has to be re-set before the next clear or
scroll.

## Redefinable character set

Font RAM holds 16 complete 256-glyph fonts inside the chip, reached through
three registers above the RTC.

| Addr | Name | Notes |
|---|---|---|
| `$FE62` | `VREG_FONT_LO` | Font byte index, low (W) |
| `$FE63` | `VREG_FONT_HI` | Font byte index, high (W). Indexes the whole font RAM, so it spans sets |
| `$FE64` | `VREG_FONT_DATA` | Data, auto-incrementing (R/W) |

These are dedicated registers rather than a mode flag on the existing data port,
because a mode flag left set by a program that crashed would send the next
screen write into the font.

`kCmdFontSet` picks which set the renderer reads. That is the `$D018` equivalent
and the reason there are 16 of them. A program can pre-build shifted copies of
its whole charset once at startup and then shift the display by one port write
per frame, which is the authentic technique rather than an approximation of it.
Sixteen sets covers 2 px phase steps of a 32 px double-height cell, and costs
64 KB of host memory and no guest address space at all.

The chip seeds every set with CP437 at construction, and a clear selects the ROM
font. So a program can switch to RAM, redefine eight glyphs, and still have the
other 248, without uploading 4 KB to change one tile. It also means no program
can strand the shell with an unreadable font.

## Fine vertical scroll

`kCmdFineY` takes a pixel offset from 0 to 31 and slides the whole scroll region
down by it. An out-of-range value clamps rather than being ignored, because a
game computing the offset from a tick counter is the normal way to overshoot.

On and off is a separate flag from the offset, because offset 0 is a position,
the wrap point of every scroll cycle, rather than a request to stop. A program
that never issues the command is unaffected, which matters because turning it on
costs a row.

### The hidden staging row

Shifting down opens a gap of `fine_y` pixels at the top of the region, and what
belongs there is the row that does not exist yet. So the region's top row is a
hidden staging row. The renderer clips the region to pixel rows
`(scroll_top+1)*char_h` through `(scroll_bot+1)*char_h - 1`, and draws region row
`r` at `r*char_h + fine_y`.

- At `fine_y = 0` the top row sits entirely above the clip and the rest fill the
  region.
- As `fine_y` grows, the top row's bottom pixels slide into view and the bottom
  row's are clipped at the region edge.
- When `fine_y` reaches the cell height, issue `kCmdScrollDown`, reset `fine_y`
  to 0, and write a fresh hidden top row.

The chip scroll and the offset reset are two halves of one step and must be
issued back to back, or the world sits a full cell low for several repaints.

The scroll region's top is settable, so the staging row is `scroll_top` rather
than being forced to row 0 and a program can pin header rows above the scrolling
band. `shiftRowFlags` masks to the same window, so a double-size band scrolls its
size flags correctly inside a partial region.

### What it cannot do

Everything in the region shifts, including objects the game drew there. A
screen-fixed object sawtooths by one cell height per tile unless it is drawn as
a sprite. This is the limitation sprites exist to remove, and it is stated in
`VIC.h` as well.

## Sprites

Seventeen sprites, six bytes each, at `$FE65-$FECA`.

| Offset | Contents |
|---|---|
| 0 | X low |
| 1 | X high: bits 1-0 position, bits 4-2 width−1, bit 5 magnify X |
| 2 | Y low |
| 3 | Y high: bits 1-0 position, bits 4-2 height−1, bit 5 magnify Y, bit 6 bitmap, bit 7 enable |
| 4 | Glyph, or pattern slot when bit 6 of offset 3 is set |
| 5 | Attribute, foreground and bright as in a cell; ignored for a bitmap sprite |

Positions are nominal pixels on the 8x16 cell grid, so 0 to 639 by 0 to 399, and
the renderer scales them by the window zoom. The background is transparent.
There are no collision registers. Games compute collision themselves, which they
were doing anyway.

Size and magnify are both ways to make a sprite bigger and they are not
interchangeable. Size composes adjacent glyph codes, so a 2x2 draws `g` through
`g+3`, buying detail at the cost of artwork across four patterns. Magnify
stretches one pattern, buying no detail but matching what a double-size row does
to a character. Sizes are stored as size−1, so code written before sizes existed
leaves those bits zero and still gets a 1x1 sprite.

Seventeen was chosen against the I/O page rather than a theoretical peak.
Twenty-five fitted and left five free bytes of the only address space new
devices have, where 17 leaves 53.

### Bitmap sprites

A glyph is one colour, and it shares its code with the text. A sprite can instead
take its picture from **sprite pattern RAM**: 256 slots of 16x16 pixels at 4 bits
a pixel, 32 KB of video RAM inside the chip. Like the font, it is not in the 64K
map. It is reached through an index and data port:

| Address | Contents |
|---|---|
| `$FECE` | Byte index low |
| `$FECF` | Byte index high. The index runs 0 to 32767, and slot n starts at n × 128 |
| `$FED0` | Data, read and write; the index advances after each and wraps at 32 KB |

Each slot is 8 bytes a row, 16 rows, top to bottom. A byte holds two pixels, the
left one in the high nibble. Pixel 0 is transparent, and 1 to 15 name palette
slots, so a palette change reaches bitmap sprites as it reaches everything else.
The cost is that palette slot 0 cannot be drawn; art that wants black maps it to
another slot.

Setting bit 6 of a sprite's Y high byte makes its glyph register name a pattern
slot. Size then composes consecutive slots rather than glyph codes, row-major and
wrapping at 255, so a sprite reaches 128x128 pixels. Magnify still doubles. A
pattern pixel is one nominal pixel, so a slot covers two cells across and one down.

To animate, upload every frame once and switch the glyph register between them.
That is one write per step, where rewriting the picture would be 128. A clear
turns bitmap mode off with the rest of the sprite state but keeps the pattern RAM,
so a program's art survives a screen clear. The RAM is zero at power-on and, like
the font, is kept across a warm reset.

This is the TMS9918's arrangement: a video chip with its own RAM, reached through
ports, holding the sprite patterns. The TMS9918 had 16 KB for everything; the
V9938 and the C128's VDC had 64 KB. `DEMOS/SPRDEMO.PRG` shows the lot: two-frame
and four-frame cycles, a figure walking in 16x32 frames of two slots each, and a
2x2 composition.

### Drawing pictures: the `.art` format

Nobody types pattern bytes. A program's pictures are drawn in an `.art` file, one
character a pixel, and `spr2c` turns them into packed C arrays at build time. The
drawing is the source and the bytes are generated, so the two cannot drift apart.
A catalog entry asks for it with `generate = NAME.art -> NAME_art.c`, and the result
compiles as one more source; `docs/cc65_to_prg.md` covers the build side.

```
# KERNEL PANIC's craft
@sprite art_craft 16x16
................
........4.......
.......CC.......
...

# the firewall barrier
@glyph glyph_fire 8x16
........
...
########
```

**Lines.** A file is a sequence of pictures. Each starts with a directive line and
is followed by exactly as many rows as its height. Between pictures, a line starting
`#` is a comment. Inside a picture every line is a row, because a glyph row can
start with `#`. Blank lines are ignored everywhere, and trailing spaces are trimmed.

**`@sprite NAME WxH`** is a bitmap-sprite picture, 4 bits a pixel. Each row is `W`
characters: `.` is transparent (pixel 0) and a hex digit `1`-`F` is that palette
slot, in either case. `W` is even and 2 to 128; `H` is 1 to 128.

**`@glyph NAME 8xH`** is a soft-font character, 1 bit a pixel, for the font port.
Each row is 8 characters: `#` is ink, drawn in the cell's foreground colour, and `.`
is ground, drawn in its background colour. `H` is 1 to 32; a font glyph is 16.

**`NAME`** becomes `const unsigned char NAME[n]`, so it has to be a C identifier and
unique in the file. The program declares the names it uses as `extern`, as it would
for a hand-written array.

**Byte layout.**

- A sprite whose width and height are both multiples of 16 is written slot by slot:
  128 bytes per 16x16 slot, in the row-major order the chip composes them in (left to
  right, then down). A whole picture therefore loads with consecutive
  `spr_img_load()` calls, and a 16x32 frame is two.
- Within a slot, and for a sprite of any other size, each row is `W/2` bytes, with
  the left pixel of each pair in the high nibble, rows top to bottom. KPANIC's
  8-pixel spread column is one of these: 4 bytes a row, which the game composes into
  volley pictures itself.
- A glyph is one byte a row, the leftmost pixel in bit 7.

**Errors.** A row of the wrong width, a character that is not allowed, a picture
with too few or too many rows, an unknown directive or a name used twice each stop
the build with the file and line. A row one character short would otherwise draw a
picture that is subtly wrong. A failed run leaves no output file behind, so the build
cannot mistake a half-written one for current.

The generated C repeats each picture in a comment above its bytes, but it lives in
the build tree and is never edited. `kpanic.art`, `venture.art` and `sprdemo.art`
are the working examples.

## The frame counter

`$FECD` counts frames, wrapping, and `K_WAIT_FRAME` ($FF42) blocks until it
changes. The point is not timing, which the jiffy counter already gave: it is
that the host presents the plane on the same boundary, so a program returning
from the wait owns the whole interval and cannot be photographed mid-update.

It is a counter and not a flag, and reading does not clear it. The VIC-II's
collision registers clear on read, which leaves room for exactly one consumer --
a debugger looking at the value destroys it for the program. A counter also
says how many frames went by, so a program that fell behind can tell, where a
flag would hide it.

The boundary is the same event as the jiffy interrupt; they are ticked together
so they cannot drift. Waiting is still not pacing. `wait_frame()` says a frame
began, not how many were missed, so a game keeps a fixed-timestep accumulator
against the jiffy counter to make up a backlog after the host stalls.

Writing `$FECD`, any value, presents. It says the frame is finished and should
be shown now. Without it the host can only show the plane at a boundary, and a
program that paints after waking at one is always a frame behind. In VENTURE
that was 15 ms of a 23 ms press. The host shows a present at once, and for any
frame that was presented it skips the boundary repaint, because that repaint
would catch the next frame half drawn. A frame that goes by with no present puts
the boundary repaint back, so a program that never presents is shown exactly as
before, and one that stops presenting, by exiting, cannot freeze the screen.

A program that presents takes on one rule in return: nothing is written to the
VIC between the present and the next `wait_frame()`. The host reads the plane
shortly after the present rather than at the instant of it, and anything written
in that gap can land in the picture.

## The raster

The frame counter says when a frame begins. The raster says where the beam is
inside one, which is what a mid-screen split needs: colour bars, a playfield that
fine-scrolls under a status bar that does not, a different font set for the lower
half of the screen.

A frame is 500 lines of machine time. Lines 0 to 399 are drawn, one for each
nominal pixel row, and 400 to 499 are blanking. The line comes from the CPU's cycle
count since the frame boundary, so at 4 MHz a line lasts about 133 cycles, some
thirty instructions -- room to set a change up between one line and the next.

| Address | Contents |
|---|---|
| `$FED1` | Read: line, bits 7-0, latching `$FED2`. Write: compare line, bits 7-0 |
| `$FED2` | Read: bit 0 line bit 8, bit 7 set during blanking. Write: compare line bit 8 |
| `$FED3` | Raster interrupt. Read: bit 7 pending, bit 0 enabled. Write: bit 0 enable, bit 7 acknowledge |

Read the low byte first. It latches the high one, so the pair is one coherent line
even if the beam crosses from 255 to 256 between the reads -- the RTC's latch, for
the same reason.

**A split** is a palette, fine-scroll or font change made on a visible line: it
shows from that line down. The chip records each such change against its line, and
at the frame boundary hands the finished frame's bands to the renderer, which draws
each band with the settings that were live for it. So a program splits the screen
by waiting for a line and changing something:

```c
for (;;) {
    wait_frame();                /* line 0: set the top band      */
    ...
    wait_line(200);              /* the beam reaches line 200     */
    ...                          /* set the bottom band           */
}
```

The details:

- **What can split:** the palette, the fine-scroll offset (`kCmdFineY`), and the
  font (`kCmdFontSet`, `kCmdFontRom`/`kCmdFontRam`). A clear or a palette reset made
  mid-frame splits too. The cell plane, the scroll region and the sprites do not.
- **Several changes on one line** make one band, holding the last of them.
- **A colour splits whole, on its blue byte.** Setting a colour is three writes, and
  they routinely straddle a line; splitting on each would draw a stripe of new red
  over old green and blue. So the split is taken when the slot's third byte arrives,
  the way a VGA DAC commits a colour. The palette itself still changes byte by byte.
- **A change in the blanking lines** has no frame left to split. It becomes the
  settings the next frame starts with.
- **A change on line 0** is the whole frame.
- **Every visible line can be a band.** The renderer skips the rows outside each
  band, so even a frame split on every line costs little more than one that is not.
  (An early cap of 64 was overflowed by colour bars plus a wobble.)
- **A frame with no mid-frame change** is drawn from the live settings, exactly as
  before the raster existed, so a program that never splits sees no difference.
- **The frame shown is the last completed one.** A split program should not present:
  its bands are only whole at the boundary, and the boundary repaint is what shows
  them.

`wait_line(n)` busy-waits until the beam is on line `n` or past it, and returns at
once if it already is, so call it in rising order within a frame. `raster_line()`
returns the line. `DEMOS/RASTER.PRG` draws both classic effects: colour bars and a
fine-scroll wobble.

### The raster interrupt

Polling spends the frame waiting. The raster interrupt lets the CPU do other work:
the VIC raises IRQ when the beam reaches a chosen line, as the C64's VIC-II did.

**The chip.** Writing `$FED1`/`$FED2` sets the compare line, the same two registers
that read the beam -- again the C64's arrangement, where `$D012` is both. When the
beam reaches the compare line the interrupt goes pending, and while it is pending and
enabled the VIC pulls the CPU's IRQ line. The machine checks after every instruction,
so the test is *crossing* the line, not landing on it; an instruction can span a line
boundary. A compare the beam has already passed waits for the next frame, which is
what a handler re-arming for its next split wants. A compare of 0 fires at the frame
boundary. `$FED3` enables it and acknowledges it, and a clear command turns it off, so
a program that quits cannot leave it calling a handler in memory it no longer owns.

**The line is shared.** IRQ is wired-OR, as on a real board: the PIA's interval
timer and the VIC's raster both pull it, and it stays asserted until the last lets go.
Reading `$FE0E` says whether the timer is one of them (bit 7), so the kernel can tell
the two apart.

**The kernel.** Its IRQ handler checks the raster first, because a split has a
deadline. A pending raster interrupt is acknowledged, then A, X and Y are saved and
the program's handler is called through `RASTER_VEC` (`$33`-`$34`). Only a pending
timer advances the jiffy. `K_RASTER_IRQ` (`$FF45`) installs a handler (address in A/X)
and enables the interrupt, or with 0 disables it and puts back a do-nothing default.
Set the compare line first. The handler is an ordinary subroutine ending in `RTS`; the
interrupt is already acknowledged, so it may set the next compare line before it
returns.

**From C: a copper list.** A raster interrupt's handler cannot be C, because cc65's
runtime keeps state in zero page that an interrupt would trample. So the glue offers
a copper list instead -- the name is the Amiga's, whose copper coprocessor ran exactly
this -- a table of (line, register, value) writes that an assembly handler makes on
their lines, frame after frame:

```c
static unsigned char list[] = {
    0, 0,   0xCB, 0,      /* line 0: palette index = slot 0 ...     */
    0, 0,   0xCC, 0,      /* ...red, green, blue = black            */
    ...
    0, 0xFF               /* the end: a line whose high byte is $FF */
};
copper_start(list);       /* and copper_stop() before leaving      */
```

Each entry is four bytes: line low, line high, then the register as the low byte of
its `$FExx` address, and the value. Entries go in rising line order; several on one
line are made together. After the last, it starts again from the top next frame. A
program animates it by rewriting values in place during the blanking lines.
`DEMOS/RASTER.PRG` draws its effects both ways, and Space switches between polling
and the copper list -- the picture is the same.

## What is still missing

- There is no bitmap mode. The display is text only, though giving a small
  region unique glyph codes per cell yields a pixel framebuffer of 128x256,
  which is the classic MSX and Amstrad trick.

Against that, there are things no period chip had. Background colour is per
cell, where the VIC-II and the VDC had one global background. The scroll command
takes a settable region, rows can be double size individually, and the block ops
cost the CPU nothing.

## Tests

`tests/test_vic_softfont.cpp` covers the font path. It checks ROM by default,
the round-trip through `$FE62-$FE64` with auto-increment, an index spanning the
whole RAM rather than 2000 cells, `kCmdFontRam` changing what `glyphRows()`
returns, `kCmdFontSet` switching sets, an out-of-range set clamping,
`kCmdFontReset` restoring CP437 everywhere, and the literal addresses.

`tests/test_vic_finescroll.cpp` has seven. Fine scroll is off by default, the
command sets the offset and turns it on, offset 0 still counts as on, an
out-of-range value clamps, `kCmdClear` turns it off, the region accessor agrees
with the scroll commands, and none of it disturbs the cell plane or the font
selection.

`tests/test_vic_sprites.cpp` covers the sprite block.

Renderer geometry is not unit-testable here and is verified by eye.
