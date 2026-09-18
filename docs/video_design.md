# Video design — smooth scrolling on a character-cell chip

Why the VIC scrolls in whole cells, and the three features added to fix it. Those
are a redefinable character set, a fine vertical scroll and sprites. This is the
counterpart to `sound_design.md`, and the register-level result is in
`architecture.md` and `board.md`.

Written because KERNEL PANIC kept running into the same wall, and the wall turned
out to be in the machine rather than the game.

All three are now implemented. This document is kept because the reasoning is what
is worth having. The order they were built in changed twice as evidence came in,
and two of the three answers were not the obvious ones. Sections below are marked
where they record a decision that was superseded.

## What the VIC is, and what it is missing

Our VIC is a CRTC and terminal-lineage chip, not a home-computer game chip. Its
ancestry is visible in the register map, which has a VDC-style address and data
port with private screen storage that is not memory-mapped, a 6845-style hardware
cursor, a CGA and VGA attribute byte, VT100 double-height lines, and a block-op
command engine. All of that comes from the text terminal side of the family tree.

What the period game chips had that this one did not, and where each now stands:

| Feature | C64 VIC-II | Status here |
|---|---|---|
| Hardware sprites | 8 x 24x21, collision registers in hardware | Added — 17, one glyph each, pixel-positioned, transparent background. No collision registers: the games compute it, which they were doing anyway |
| Fine scroll | `XSCROLL`/`YSCROLL`, 0-7 px | Added — pixel offset on the scroll region |
| Redefinable character set | char base points anywhere in RAM | Added — 16 switchable 256-glyph sets in chip RAM |
| Raster register + IRQ | `$D012` + compare interrupt | Still missing. No idea where the beam is; no split screens, no mid-frame changes |
| Bitmap mode | 320x200 hires | Still missing. Text only — though unique glyph codes over a small region give a pixel framebuffer, the classic MSX/Amstrad trick |

For balance, there are things here that no period chip had. Background colour is
per cell, where the VIC-II and the VDC had one global background. The scroll
command takes a settable region, rows can be double size individually, and the
block ops cost the CPU nothing, where a real VIC-II stole around 40 cycles per
badline and the VDC made you poll a status bit.

There is also no vblank signal. Three independent 60 Hz timers run in the host for
CPU execution, the PIA timer that drives the jiffy counter, and the display
refresh, and nothing locks them together. A program cannot sync to the display, so
an occasional step is painted twice or skipped.

## The actual problem: quantum, not rate

The scroll quantum is one character cell, 16 px on a normal row and 32 px on a
double-size one. At six steps a second a 32 px jump reads as a strobe rather than
motion, and slowing the tick cannot help, because it only spaces the same jumps
further apart.

KERNEL PANIC first tried double-size rows for bigger glyphs, which doubled the
quantum and made it worse. It now runs 80x24 single rows at a 16 px quantum. That
is the cheap half of the fix, and the rest needs the chip.

## Which fix, and what each one cannot do

Fine scroll shifts the whole scroll region, including anything the game drew into
it, so a screen-fixed player craft bobs down with the terrain and snaps back one
cell height on every hardware scroll. On a real machine the player is a sprite and
is unaffected. At the time this was written, this machine had none. It does now,
and that is exactly what they were added for.

The soft font instead shifts glyph patterns. Rephase the terrain glyphs, leave the
object glyphs alone, and nothing bobs. But a solid glyph is shift-invariant, so it
cannot smooth a solid wall at all.

Prototyping settled the question, and "What the prototype showed" below has the
detail. Glyph rephasing moves only the things whose glyphs are rephased, and a
cell-plane object cannot usefully be one. Counting KERNEL PANIC's moving parts:

| object | moves with | fine scroll | glyph rephase |
|---|---|---|---|
| conduit terrain | the world | smooth | smooth |
| data nodes | the world | smooth | smooth |
| enemies | the world | smooth | judders |
| enemy pellets | the world +1 | smooth | judders |
| weapon fragments | the world | smooth | judders |
| your shots | screen, upward | judders | judders |
| your craft | screen, fixed | judders | judders |
| | | 5/7 smooth | 2/7 smooth |

Fine scroll wins because everything that rides the world is inside the scroll
region and moves with it for free. Its two failures are precisely the two
screen-referenced objects, the craft and your shots, which is exactly what sprites
are for.

So the destination is fine scroll plus a small sprite set, and the sprites only
have to cover the player's own craft and shots rather than the whole cast. The
soft font is a real capability for tile art, animation and pseudo-bitmap work, but
it is not the route to smooth scrolling in a game with objects in the playfield.

---

## What the prototype showed

Before reworking any terrain, the pair-glyph scheme was prototyped on the host. A
cell's appearance is derived from its own terrain type and the type of the cell
above it, and at phase k the glyph shows k pixel rows of the one above followed by
its own. There were two results.

The mechanism works. Dumped as ASCII art, the boundary between wall and lane
descends exactly one pixel row per phase step, and it stays correct across the
hardware scroll because both cells move down together, so the pair of types a
glyph encodes is preserved. Same-type pairs such as wall over wall and lane over
lane are shift-invariant and need only one glyph, so the budget is modest.
KERNEL PANIC's terrain needs about 84 codes, because whether a cell is trace or
blank is decided by column and so never changes as you go down one.

But it only moves the terrain. Everything else in the playfield is a cell-plane
object drawn with its own glyph, and rephasing an object glyph does not move the
object. To sit at a sub-row position it would have to straddle two cells, and a
glyph cannot composite itself over the terrain behind it. That is not a limitation
of the scheme but the thing sprites exist to solve, and the table above is the
consequence.

## Part A — Redefinable character set

### Which model, and why it needs font sets

Period video chips split two ways. The C64's VIC-II, Atari's ANTIC and the Apple
II all read main RAM, so screen, charset and sprite data sit at addresses the CPU
can see and redefining a character is a plain `STA`, at the cost of bus contention.
The TMS9918A in the MSX and the C128's VDC 8563 instead have private VRAM behind an
auto-incrementing address and data port, which costs no address space and causes no
contention but makes every byte a port write.

Our VIC is already the second kind. `screen_buffer_` and `color_buffer_` are
private to the chip and reachable only through `VREG_ADDR_*` with `VREG_CHAR` and
`VREG_COLOR`. Font RAM behind the same style of port is consistent with this
machine and with the VDC it was modelled on.

But that model forbids the trick that makes charset scrolling cheap. On a C64 you
do not rewrite characters per frame. You pre-build eight shifted copies of the
whole charset and repoint the character base with one byte written to `$D018`.
There is no addressable font base here to repoint, so the equivalent has to be a
set select.

- Font RAM holds `kFontSets` complete 256-glyph fonts, and 16 is ample. That is
  16 x 4 KB of host memory and no guest address space at all.
- It is a set rather than a bank. `MODULE_BANK` at `$FE23` already means switching
  ROM into the `$B000-$EFFF` window, and this is unrelated. It is internal storage
  inside the chip, picked by a command, never visible to the 6502.
- `kCmdFontSet` takes a set index as its parameter and selects which one the
  renderer reads.
- The upload port still exists, for building the sets once at startup or for a
  program that genuinely wants to animate a glyph.

Scrolling then costs one port write per frame instead of an upload, which is the
authentic technique rather than an approximation of it.

### Protocol

Private font RAM inside the VIC, `kFontSets` by 256 glyphs by 16 rows, reachable
only through registers. It costs the guest no address space, which is why this
beats mapping a font into user RAM. User RAM at `$0800-$87FF` is only 32 KB and
VAULT already nearly overflowed it.

The VIC block `$FE2D-$FE37` is full and `$FE38` is the SID, so the new registers go
in the free space above the RTC. `$FE61` is the PowerSwitch and `$FE62` was the
first free byte.

| Addr | Name | Notes |
|---|---|---|
| `$FE62` | `VREG_FONT_LO` | font byte index low (W) |
| `$FE63` | `VREG_FONT_HI` | font byte index high (W). Indexes the whole font RAM, so it spans sets; 16-bit means the RAM can grow later without a protocol change |
| `$FE64` | `VREG_FONT_DATA` | R/W, auto-increments |

Four commands ride the existing engine. `kCmdFontRam` and `kCmdFontRom` select
which the renderer reads, `kCmdFontReset` reloads CP437 into every set, and
`kCmdFontSet` takes a set index and picks the live set. That last one is the
`$D018` equivalent and the one written per frame. Check `VIC.h` for the next free
command code, since `kCmdScrollTop` already took `0x07`.

These are dedicated registers rather than a font-access-mode flag on the existing
data port, because a mode flag left set by a program that crashed would send the
next screen write into the font.

### Why reset seeds font RAM with CP437

`reset()` copies `kCp437Font` into font RAM, and `kCmdClear` selects ROM. So a
program can switch to RAM, redefine eight glyphs, and the other 248 are still
CP437, with no need to upload 4 KB to change one tile. It also means no program can
strand the shell with an unreadable font.

### Host changes

- `include/computer/VIC.h` and `src/computer/VIC.cpp` gain the three registers, a
  `std::array<uint8_t, kFontSets * 4096> font_ram_`, `font_index_`, `font_set_`,
  `font_ram_active_`, the four commands, and
  `[[nodiscard]] const uint8_t *glyphRows(uint8_t glyph) const`, which returns
  either the ROM row or the live set's row.
- `isVideoRegAddress` becomes two ranges. Update the boundary tests that pin the
  literal addresses, following the convention the RTC and PIA tests set.
- `src/computer/Memory.cpp` routes `$FE62-$FE64` to the VIC.
- In `src/ui/DisplayWidget.cpp`, `blitGlyph` reads `video_chip_->glyphRows(glyph)`
  instead of `&Computer::kCp437Font[glyph * 16]`. That is one line, because
  everything else already goes through it.
- `docs/architecture.md` and `board.md` need the I/O table and the chip's
  description updated. The sprite block later took `$FE65-$FECA`, so `$FECB` is
  the first free byte now.
- While in `VIC.h`, delete the class comment describing a legacy memory-mapped
  window at `$0400-$07E7`, which no longer exists in the code and contradicts the
  private-plane architecture the font RAM is built on.

### Game side (KERNEL PANIC)

- Reserve one block of glyph codes for terrain (wall, bevel, lane, board) and a
  separate block for objects (craft, corruption, shots, nodes, fragments), so
  rephasing terrain never moves an object.
- Build the phase sets once at startup. Each tick is then a single `kCmdFontSet`
  write to phase `k`, and when `k` wraps past the cell height, do the hardware
  `SCROLLDOWN` and generate a new row exactly as today.
- Eight phases of a 16 px cell gives 2 px steps.
- The tile art is constrained. A rephased glyph can only be generated from itself,
  so terrain patterns must tile vertically with a period equal to the cell height.
  Blanks and vertical traces already do. The horizontal trace runs and solder pads
  do not, and have to be redesigned as part of a vertically-repeating strip, or
  dropped.
- The limitation that matters most is not obvious. A solid glyph is
  shift-invariant, so rephasing CP437 219 produces the identical glyph and the
  conduit walls get no sub-cell motion at all. Their apparent movement comes from
  the channel's silhouette changing shape row to row, which only updates on the
  whole-cell hardware scroll. Phase sets therefore smooth the texture while the
  thing the eye actually tracks, the wall edge being dodged, still steps a full
  cell.

  Smoothing the silhouette needs authored transition tiles, glyphs that are wall
  for the top k pixels and channel below, one per way the edge can move. The
  channel meanders at most one column per row, so that is three cases of same,
  left and right, times two sides, times eight phases, or about 48 glyphs. That is
  well inside 256 and feasible, but it is art and bookkeeping rather than a
  register, and it is the real cost of Part A.

### Tests

`tests/test_vic_softfont.cpp` checks that the chip defaults to ROM, that font RAM
round-trips through `$FE62-$FE64` with auto-increment, and that the index spans
the whole RAM rather than just 2000 cells. It checks that `kCmdFontRam` changes
what `glyphRows()` returns, that `kCmdFontSet` switches sets and `glyphRows()`
follows, and that an out-of-range set is clamped or ignored. It checks that
`kCmdFontReset` restores CP437 in every set, that `reset()` seeds CP437 and
selects ROM and set 0, and that the literal addresses are pinned.

---

## Part B — Fine vertical scroll

### Protocol

This rides the command engine, matching the precedent set by double-size rows in
`kCmdRowSize` and the scroll-region top in `kCmdScrollTop`, so it needs no address
space.

- `kCmdFineY` is `0x0C` and takes a pixel offset from 0 to 31 in `VREG_CMD_PARAM`.
  An out-of-range value is clamped rather than ignored, because the furthest the
  region can slide is an obvious right answer and a game computing the value from
  a tick counter is the normal way to overshoot.
- It shifts the contents of the scroll region, `scroll_top` through `scroll_bot`,
  down by that many pixels, and turns fine scrolling on. `kCmdClear` and `reset()`
  turn it off.
- The on and off flag is separate from the offset, because offset 0 is a position
  rather than a request to stop. It is the wrap point of every scroll cycle. If
  zero meant off, the staging row would pop into view for one frame on every cell
  boundary. A program that never issues the command is completely unaffected,
  which matters because turning it on costs a row.
- Like `kCmdRowSize` and `kCmdScrollTop` it consumes the shared command parameter,
  so the fill char must be re-set before the next clear or scroll.

### The hidden staging row

Shifting down opens a gap of `fine_y` pixels at the top of the region, and what
belongs there is the row that does not exist yet. So the region's top row is a
hidden staging row. The renderer clips the region to pixel rows
`(scroll_top+1)*char_h` through `(scroll_bot+1)*char_h - 1`, and draws region row
`r` at `r*char_h + fine_y`.

- At `fine_y = 0` the top row sits entirely above the clip and is invisible, and
  the rest of the rows fill the region.
- As `fine_y` grows, the top row's bottom `fine_y` pixels slide into view and the
  bottom row's are clipped off at the region edge. Both are correct for a downward
  scroller.
- When `fine_y` reaches the cell height, issue `SCROLLDOWN`, set `fine_y` back to
  0, and write a fresh hidden top row.

This costs exactly one row of playfield. The scroll-region top, added for TERM's
DECSTBM, makes it better than it would otherwise have been. The staging row is
`scroll_top` rather than being forced to row 0, so a program can pin header rows
above the scrolling band, and `shiftRowFlags` masks to the top-to-bottom window so
a double-size band still scrolls its size flags correctly inside a partial
region.

### Host changes — DONE

- `VIC.h` and `VIC.cpp` gained `fine_y_`, `fine_active_` and `kCmdFineY`, plus
  `fineY()`, `fineActive()` and `getScrollRegion()` for the renderer to read.
- In `DisplayWidget::paintEvent` the offset goes into each cell's destination rect
  through a new `y_offset` argument on `drawCharacterAt` and `blitGlyph`, and the
  clip is scoped to the cell loop with `save()` and `restore()`. This is
  deliberately not a `painter.translate()` or a frame-wide clip, because a later
  sprite pass would inherit either and sprites are exactly the things that must
  not move with the region.

### Tests

`tests/test_vic_finescroll.cpp` has seven tests. Fine scroll is off by default,
the command sets the offset and turns it on, offset 0 still counts as on, an
out-of-range value clamps, `kCmdClear` turns it off, the region accessor agrees
with the scroll commands, and it disturbs neither the cell plane nor the font
selection it shares a command engine with.

Renderer geometry is not unit-testable here, so verify it by eye. The two safety
behaviours were mutation-checked. Removing the reset from `cmdClear` fails
`ClearTurnsItOff`, and treating offset 0 as a stop fails
`ZeroOffsetStillCountsAsOn`.

### Known limitation to state in the header comment

Everything in the region shifts, including objects the game drew there. A
screen-fixed player will sawtooth by one cell height per tile unless it is drawn as
a sprite, which at the time of writing did not exist, or its glyph is rephased
through Part A. Say so plainly in `VIC.h` so the next person does not rediscover it
in a play-test.

---

## Effect on a future sprite implementation — as it turned out

Checked before sprites were built, because they were the likely next chip feature.
Recorded because the prediction held. The address space was ample, the soft font did
supply the pattern RAM and upload protocol, and the painter-translate hazard was
real enough that avoiding it was the first thing written into the renderer.

Address space was no problem. Nothing was mapped above `$FE61`, so the 158 bytes of
`$FE62-$FEFF` were free, and Part A took three of them, leaving 155 contiguous. The
VIC-II fitted eight sprites into 47 bytes.

Part A helps, because it builds exactly what sprites need anyway. That is private
pattern RAM in the chip, plus an address pair and an auto-incrementing data port to
upload into it. Sprites can index font RAM as their pattern table, the way the
TMS9918 and the VDC do, or reuse the protocol for their own. The font index
register is 16-bit, so the RAM can grow past 4096 bytes later without a protocol
change.

Part B carries one hazard, and it belongs in the implementation. Apply the fine
offset in the per-cell rect arithmetic rather than as a `painter.translate()` for
the frame, and scope the clip to the cell pass only. A global translate would be
inherited by a later sprite pass, so sprites would bob with the terrain, which is
precisely the bug sprites are being added to fix, and a frame-wide clip would cut
them off at the playfield edge.

Two smaller notes. `blitGlyph` is opaque today, painting foreground for 1 bits and
background for 0 bits, and sprites need an ARGB path that skips background pixels.
That is a separate blit, so Part A's one-line change there does not constrain it.
At the game level, terrain scrolled by glyph rephasing plus a pixel-positioned
sprite player means the game must know the current phase to align collision. That
is normal tile-scrolling bookkeeping, but the two scrolling routes are not
interchangeable from the game side.

One thing had to be decided before building, which is whether sprite patterns share
font RAM or get their own region. Either is fine given the 16-bit index, but
choosing early avoided a layout worth redoing later.

## Sequencing — as built

1. Soft font. Self-contained, no game changes. Done.
2. Fine scroll. The prototype promoted this ahead of the terrain rework, because
   it smooths five of KERNEL PANIC's seven moving parts against rephasing's two.
   Done.
3. Sprites, 17 of them. KERNEL PANIC uses one for the craft and 16 for its shots,
   which are the two things fine scroll cannot place correctly. Done.
4. KERNEL PANIC terrain rephasing, demoted and not done. Worth doing for tile art
   and animation, but not as a scrolling mechanism.

What play-testing then found, in order, because each one only became visible once the
one before it was fixed:

- Fine scroll bounced the entire playfield. The chip scroll and the offset reset
  are two halves of one step and must be issued back to back. The reset was
  happening after some 13 ms of row generation, so the world sat a full cell low
  for several repaints.
- Objects blinked. A cell-plane object is absent from the screen for the whole gap
  between its erase and its redraw, and the host repaints several times inside it.
  Deferring the 80-cell row paint to the next frame cut that window by a factor of
  4.5, and moving the craft and shots to sprites removed it for them entirely.
- The craft still sawtoothed, which was the predicted limitation rather than a bug.
  The toggle that proved it, turning fine scroll off and watching the craft go
  steady, was worth more than any amount of further reasoning. Sprites fixed it.

Sprite sizing was chosen against the I/O page rather than against a theoretical
peak. Twenty-five sprites fitted and left only five free bytes of the only address
space new devices have, where 17 leaves 53. The shot pool is 16 to match, and a
volley is all-or-nothing so that a full pool costs fire rate rather than silently
narrowing the gun. `fire()` spawns the centre shot first and works outwards, so
dropping individual shots eats the wings.

A side-scroller came up as an alternative and was rejected. Horizontal motion is
inherently finer, since a cell is wider than it is tall and so a column step is
half a row step, but there is no hardware horizontal scroll. Every frame would mean
rewriting the whole playfield, and 40x12 cells is roughly 50 to 60 ms, capping the
frame rate near 16 to 20 a second with nothing left for objects. Smoother per step,
slower per frame, and far more work.

Two things are still on the horizon. With a soft font you can give a small region
unique glyph codes per cell and get a true pixel framebuffer, since 256 glyphs is a
128x256 px window, which is the classic MSX and Amstrad trick. And a raster
register or vblank signal remains the most obvious gap, because three independent
60 Hz timers run in the host with nothing locking them, so a program still cannot
sync to the display.

## Risks

- The tile art rather than the code is the hard part of Part A. If the board
  backdrop cannot be made vertically periodic, the horizontal runs will visibly pop
  every phase cycle. Prototype one wall and one lane glyph set before rephasing
  everything.
- Two non-contiguous VIC register ranges is mild ugliness, and the alternative of a
  stateful font-access mode on the existing port is worse.
- `blitGlyph` builds a `QImage` per cell per frame today. A RAM font does not change
  that cost, but it does mean a future glyph cache would need invalidating on upload
  and on set-switch.
