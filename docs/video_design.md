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
| 3 | Y high: bits 1-0 position, bits 4-2 height−1, bit 5 magnify Y, bit 7 enable |
| 4 | Glyph |
| 5 | Attribute, foreground and bright as in a cell |

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

## What is still missing

- There is no raster register and no vblank signal. Three independent 60 Hz
  timers run in the host and nothing locks them, so a program cannot sync to the
  display and an occasional step is painted twice or skipped. This is the most
  obvious gap.
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
