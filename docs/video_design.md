# Video design — smooth scrolling on a character-cell chip

Design notes for two proposed VIC features: a **redefinable character set** and a
**fine vertical scroll**. The counterpart to `sound_design.md`; the chip as it stands
today is documented in `BOARD.md` and `ARCHITECTURE.md`.

Written because KERNEL PANIC kept running into the same wall, and the wall turned out
to be in the machine rather than the game.

## What the VIC is, and what it is missing

Our VIC is a **CRTC/terminal-lineage chip**, not a home-computer game chip. Its
ancestry is visible in the register map: a VDC-style address+data port with private
(non-memory-mapped) screen storage, a 6845-style hardware cursor, a CGA/VGA attribute
byte, VT100 double-height lines, and a block-op command engine. All from the text
terminal side of the family tree.

What the period *game* chips had that we do not:

| Missing | C64 VIC-II | What it costs us |
|---|---|---|
| Hardware sprites | 8 x 24x21, collision registers in hardware | Every moving object is a character cell — grid-locked in both axes, cannot overlap, cannot be positioned to the pixel |
| Fine scroll | `XSCROLL`/`YSCROLL`, 0-7 px | The scroll quantum is a whole cell |
| Raster register + IRQ | `$D012` + compare interrupt | No idea where the beam is; no split screens, no mid-frame changes |
| Redefinable character set | char base points anywhere in RAM | No custom tiles, and no sub-cell scrolling trick |
| Bitmap mode | 320x200 hires | Text only |

Things we have that no period chip did, for balance: per-cell **background** colour
(the VIC-II and VDC had one global background), a scroll *command* with a settable
region, per-row double size, and block ops that cost the CPU nothing — a real VIC-II
stole ~40 cycles per badline, and the VDC made you poll a status bit.

There is also **no vblank signal**. Three independent ~60 Hz timers run in the host
(CPU execution, the PIA timer that drives the jiffy counter, and the display refresh)
and nothing locks them, so a program cannot sync to the display and an occasional step
is painted twice or skipped.

## The actual problem: quantum, not rate

The scroll quantum is one character cell — 16 px on a normal row, 32 px on a
double-size one. At 6 steps/sec a 32 px jump reads as a strobe rather than motion, and
slowing the tick cannot help: it only spaces the same jumps further apart.

KERNEL PANIC first tried double-size rows for bigger glyphs, which *doubled* the
quantum and made it worse; it now runs 80x24 single rows at a 16 px quantum. That is
the cheap half of the fix. The rest needs the chip.

## Which fix, and what each one cannot do

Fine scroll shifts the **whole scroll region**, including anything the game drew into
it — so a screen-fixed player craft bobs down with the terrain and snaps back one cell
height on every hardware scroll. On a real machine the player is a sprite and is
unaffected; we have no sprites.

The soft font instead shifts **glyph patterns**: rephase the terrain glyphs, leave
object glyphs alone, and nothing bobs. But a *solid* glyph is shift-invariant, so it
cannot smooth a solid wall at all.

| | smooths texture | smooths silhouette | player bobs |
|---|---|---|---|
| soft font, texture only | yes | **no** | no |
| soft font + transition tiles | yes | yes | no |
| fine scroll | yes | yes | **yes** — needs sprites |

**The real destination is fine scroll + a few sprites.** The soft font is a cheaper
partial and a capability worth having regardless (tile art, animation, pseudo-bitmap),
not a substitute for sprites.

---

## Part A — Redefinable character set

### Which model, and why it needs font sets

Period video chips split two ways. The C64's VIC-II, Atari's ANTIC and the Apple II
all read **main RAM** — screen, charset and sprite data sit at addresses the CPU can
see, so redefining a character is a plain `STA`, at the cost of bus contention. The
TMS9918A (MSX) and the C128 VDC 8563 instead have **private VRAM behind an
auto-incrementing address+data port**, costing no address space and causing no
contention, but making every byte a port write.

Our VIC is already the second kind: `screen_buffer_` and `color_buffer_` are private
to the chip and reachable only through `VREG_ADDR_*` + `VREG_CHAR`/`VREG_COLOR`. Font
RAM behind the same style of port is consistent with this machine and with the VDC it
was modelled on.

But that model forbids the trick that makes charset scrolling cheap. On a C64 you do
**not** rewrite characters per frame — you pre-build 8 shifted copies of the whole
charset and repoint the character base with **one byte to `$D018`**. There is no
addressable font base here to repoint, so the equivalent has to be a **set select**:

- Font RAM holds `FONT_SETS` complete 256-glyph fonts (16 is ample). 16 x 4 KB = 64 KB
  of *host* memory and zero guest address space.
- **"Set", not "bank".** `MODULE_BANK` (`$FE23`) already means switching ROM into the
  `$B000-$EFFF` window. This is unrelated: internal storage inside the chip, picked by
  a command, never visible to the 6502.
- `kCmdFontSet`, parameter = set index, selects which one the renderer reads.
- The upload port still exists, for building the sets once at startup (or for a
  program that genuinely wants to animate a glyph).

Scrolling then costs **one port write per frame** instead of an upload — the authentic
technique rather than an approximation of it.

### Protocol

Private font RAM inside the VIC, `FONT_SETS` x 256 glyphs x 16 rows, reachable only
through registers. Costs the guest no address space, which is why this beats mapping a
font into user RAM (`$0800-$87FF` is only 32 KB and VAULT already nearly overflowed
it).

New registers. The VIC block `$FE2D-$FE37` is full and `$FE38` is the SID, so these go
in the free space above the RTC — `$FE61` is the PowerSwitch, `$FE62` is the first
free byte:

| Addr | Name | Notes |
|---|---|---|
| `$FE62` | `VREG_FONT_LO` | font byte index low (W) |
| `$FE63` | `VREG_FONT_HI` | font byte index high (W). Indexes the whole font RAM, so it spans sets; 16-bit means the RAM can grow later without a protocol change |
| `$FE64` | `VREG_FONT_DATA` | R/W, **auto-increments** |

Commands on the existing engine: `kCmdFontRam` / `kCmdFontRom` select which the
renderer reads, `kCmdFontReset` reloads CP437 into every set, and `kCmdFontSet`
(param = set index) picks the live set — the `$D018` equivalent, and the one written
per frame. **Check `VIC.h` for the next free command code**; `kCmdScrollTop` already
took `0x07`.

Dedicated registers rather than a "font access mode" flag on the existing data port: a
mode flag left set by a program that crashed would send the next screen write into the
font.

### Why reset seeds font RAM with CP437

`reset()` copies `kCp437Font` into font RAM, and `kCmdClear` selects ROM. So a program
can switch to RAM, redefine 8 glyphs, and the other 248 are still CP437 — no need to
upload 4 KB to change one tile. It also means no program can strand the shell with an
unreadable font.

### Host changes

- `include/computer/VIC.h` / `src/computer/VIC.cpp` — the three registers, a
  `std::array<uint8_t, kFontSets * 4096> font_ram_`, `font_index_`, `font_set_`,
  `font_ram_active_`, the four commands, and
  `[[nodiscard]] const uint8_t *glyphRows(uint8_t glyph) const` returning either the
  ROM row or the live set's row.
- `isVideoRegAddress` becomes two ranges. Update the boundary tests that pin the
  literal addresses — the RTC/PIA tests set the convention.
- `src/computer/Memory.cpp` — route `$FE62-$FE64` to the VIC.
- `src/ui/DisplayWidget.cpp` — `blitGlyph` reads `video_chip_->glyphRows(glyph)`
  instead of `&Computer::kCp437Font[glyph * 16]`. One line; everything else already
  goes through it.
- `docs/ARCHITECTURE.md` — I/O table, plus a note that `$FE65` becomes the first free
  byte.
- While in `VIC.h`: its class comment still describes a "legacy memory-mapped window at
  `$0400-$07E7`" that no longer exists in the code. Delete it — it contradicts the
  private-plane architecture the font RAM is built on.

### Game side (KERNEL PANIC)

- Reserve one block of glyph codes for terrain (wall, bevel, lane, board) and a
  separate block for objects (craft, corruption, shots, nodes, fragments), so
  rephasing terrain never moves an object.
- Build the phase sets **once** at startup. Each tick is then a single `kCmdFontSet`
  write to phase `k`; when `k` wraps past the cell height, do the hardware
  `SCROLLDOWN` and generate a new row exactly as today.
- 8 phases of a 16 px cell = 2 px steps.
- **Constraint on the tile art:** a rephased glyph can only be generated from itself,
  so terrain patterns must tile vertically with period = cell height. Blanks and
  vertical traces already do. The horizontal trace runs and solder pads do not, and
  have to be redesigned as part of a vertically-repeating strip, or dropped.
- **The limitation that matters most, and it is not obvious.** A solid glyph is
  shift-invariant: rephasing CP437 219 produces the identical glyph, so the conduit
  walls get **no sub-cell motion at all**. Their apparent movement comes from the
  channel's silhouette changing shape row to row, which only updates on the whole-cell
  hardware scroll. Phase sets therefore smooth the *texture* while the thing the eye
  actually tracks — the wall edge being dodged — still steps a full cell.

  Smoothing the silhouette needs **authored transition tiles**: glyphs that are wall
  for the top k pixels and channel below, one per way the edge can move. The channel
  meanders at most one column per row, so that is 3 cases (same / left / right) x 2
  sides x 8 phases, about 48 glyphs — well inside 256, and feasible. But it is art and
  bookkeeping, not a register, and it is the real cost of Part A.

### Tests

`tests/test_vic_softfont.cpp`: defaults to ROM; font RAM round-trips through
`$FE62-$FE64` with auto-increment; the index spans the whole RAM, not just 2000 cells;
`kCmdFontRam` changes what `glyphRows()` returns; `kCmdFontSet` switches sets and
`glyphRows()` follows; an out-of-range set is clamped or ignored (decide and pin it);
`kCmdFontReset` restores CP437 in every set; `reset()` seeds CP437, selects ROM and
set 0; the literal addresses are pinned.

---

## Part B — Fine vertical scroll

### Protocol

Rides the command engine, matching the precedent set by double-size rows
(`kCmdRowSize`) and the scroll-region top (`kCmdScrollTop`), so it needs no address
space:

- `kCmdFineY`, parameter = 0..31 pixel offset in `VREG_CMD_PARAM`.
- Shifts the contents of the scroll region (`scroll_top..scroll_bot`) **down** by that
  many pixels. Reset to 0 by `kCmdClear` and by `reset()`.
- Like `kCmdRowSize` and `kCmdScrollTop` it consumes the shared command parameter, so
  the fill char must be re-set before the next clear or scroll.

### The hidden staging row

Shifting down opens a gap of `fine_y` pixels at the top of the region, and what
belongs there is the row that does not exist yet. So **the region's top row is a
hidden staging row**: the renderer clips the region to pixel rows
`(scroll_top+1)*char_h .. (scroll_bot+1)*char_h - 1`, and draws region row `r` at
`r*char_h + fine_y`.

- `fine_y = 0` — the top row sits entirely above the clip, invisible; the rest fill it.
- `fine_y` grows — the top row's bottom `fine_y` pixels slide into view, and the bottom
  row's are clipped off at the region edge. Both correct for a downward scroller.
- `fine_y` reaches the cell height — issue `SCROLLDOWN`, set `fine_y = 0`, write a
  fresh hidden top row.

Costs exactly one row of playfield. The scroll-region *top* (added for TERM's DECSTBM)
makes this better than it would have been: the staging row is `scroll_top` rather than
being forced to row 0, so a program can pin header rows above the scrolling band, and
`shiftRowFlags` masks to the top..bottom window so a double-size band still scrolls its
size flags correctly inside a partial region.

### Host changes

- `VIC.h` / `VIC.cpp` — `fine_y_`, the command, `[[nodiscard]] uint8_t fineY() const`.
- `DisplayWidget::paintEvent` — offset and clip the region; rows outside it are
  unaffected, so a pinned HUD stays put.

### Tests

`tests/test_vic_finescroll.cpp`: defaults to 0; the command sets it; `kCmdClear` and
`reset()` zero it; values >= cell height are rejected or clamped (decide and pin it).
Renderer geometry is not unit-testable here — verify by eye.

### Known limitation to state in the header comment

Everything in the region shifts, including objects the game drew there. A screen-fixed
player will sawtooth by one cell height per tile unless it is drawn as a sprite (which
we do not have) or its glyph is rephased via Part A. Say so plainly in `VIC.h` so the
next person does not rediscover it in a play-test.

---

## Effect on a future sprite implementation

Checked deliberately, because sprites are the likely next chip feature.

**Address space: no problem.** Nothing is mapped above `$FE61`, so `$FE62-$FEFF` (158
bytes) is free; Part A takes three, leaving 155 contiguous. The VIC-II fitted 8 sprites
in 47 bytes.

**Part A helps.** It builds exactly what sprites need anyway: private pattern RAM in
the chip plus an address-pair + auto-incrementing data port to upload into it. Sprites
can index font RAM as their pattern table (the TMS9918/VDC model) or reuse the protocol
for their own. The font index register is 16-bit, so the RAM can grow past 4096 bytes
later **without a protocol change**.

**Part B carries one hazard — write it into the implementation.** Apply the fine offset
in the per-cell rect arithmetic, **not** as a `painter.translate()` for the frame, and
scope the clip to the cell pass only. A global translate would be inherited by a later
sprite pass, so sprites would bob with the terrain — precisely the bug sprites are being
added to fix — and a frame-wide clip would cut them off at the playfield edge.

Two smaller notes:

- `blitGlyph` is opaque today (fg for 1 bits, bg for 0 bits). Sprites need an ARGB path
  that skips background pixels; that is a separate blit, so Part A's one-line change
  there does not constrain it.
- Game-level: terrain scrolled by glyph rephasing plus a pixel-positioned sprite player
  means the game must know the current phase to align collision. Normal tile-scrolling
  bookkeeping, but the two scrolling routes are not interchangeable from the game side.

**Decide before building:** whether sprite patterns share font RAM or get their own
region. Either is fine given the 16-bit index, but choosing now avoids a layout worth
redoing later.

## Sequencing

1. **Part A host side** + the soft-font test. Self-contained, no game changes.
2. **KERNEL PANIC terrain rephasing** — the tile-art rework, then 2 px scrolling.
3. **Part B** — cheap once A is in, and useful for the next scroller; on its own it
   would send the game backwards.

Not in scope, recorded so the ordering makes sense: sprites are what make Part B fully
useful and what a Scramble-style side-scroller really wants. (A side-scroller is
otherwise unattractive here: horizontal motion is inherently finer — a cell is wider
than it is tall — but there is no hardware horizontal scroll, so every frame would mean
rewriting the whole playfield.) With a soft font you can also give a small region unique
glyph codes per cell and get a true pixel framebuffer — 256 glyphs is a 128x256 px
window — which is the classic MSX/Amstrad trick and the horizon this opens up.

## Risks

- **The tile art, not the code, is the hard part of Part A.** If the board backdrop
  cannot be made vertically periodic, the horizontal runs will visibly pop every phase
  cycle. Prototype one wall and one lane glyph set before rephasing everything.
- Two non-contiguous VIC register ranges is mild ugliness; the alternative — a stateful
  font-access mode on the existing port — is worse.
- `blitGlyph` builds a `QImage` per cell per frame today. A RAM font does not change
  that cost, but it does mean a future glyph cache would need invalidating on upload and
  on set-switch.
