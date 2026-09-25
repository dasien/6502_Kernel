# MFC Host GUI

The Qt front end. This covers the window and widget structure, how the character
plane, sprites and cursor are rendered, and how host input reaches the machine.
The virtual chipset behind all of it is described in [board.md](board.md).

---

## Window structure

`MainWindow` holds a horizontal layout with two children.

- `DisplayWidget`, the machine's screen. It sizes itself to
  `VIC::kScreenWidth * char_width_` by `VIC::kScreenHeight * char_height_`,
  which at the default 8x16 cell is 640x400 pixels for the 80x25 plane.
- A CPU register sidebar, a plain `QWidget` of fixed width 110 pixels. It is
  hidden at construction and shown only from View, so the normal window is the
  display and nothing else.

There is no power button and no on-screen controls. The machine powers on during
construction and the display takes keyboard focus programmatically, so the window
is live as soon as it appears. Everything else is a menu action.

| Menu | Action | Shortcut |
|---|---|---|
| File | Exit | `Ctrl+Shift+Q` |
| Control | Reset | `Ctrl+Shift+R` |
| Control | NMI | `Ctrl+Shift+N` |
| View | CPU Registers | |
| View | Zoom 1x / 2x / 3x | `Ctrl+1` / `Ctrl+2` / `Ctrl+3` |
| Help | About | |

The machine shortcuts all carry Shift on purpose. A bare `Ctrl+`letter is a
control byte the guest wants. Without Shift, `Ctrl+R` would reset the machine
instead of starting TERM's XMODEM receive, and the menu would win, because a
shortcut resolves before the focused widget's `keyPressEvent`.

Zoom is an integer scale with nearest-neighbour sampling, so the pixels stay
crisp. The window resizes to fit the chosen factor and a black surround takes up
any slack.

## Timers

Two run in the host.

- The execution timer fires every 1 ms and runs the CPU against a time-based
  budget. The 60 Hz interval-timer IRQ is derived inside `runCycles()` at
  `clockHz / 60` cycles rather than from a second host timer, so the jiffy tick
  and the CPU speed cannot drift apart.
- A 100 ms status timer refreshes the register sidebar.

`DisplayWidget` has no clock of its own. The execution timer repaints it after a
slice when `Computer6502::takeRepaintDue()` says so, which is on a present or at an
emulated frame boundary. `docs/board.md` has the rule and the rest of the line
between the machine and the host.

## Rendering

The character and colour planes are not in the 64K map. They live inside the VIC
and the kernel reaches them through the register port at `$FE2D-$FE37`, so the
renderer reads them back out of the chip.

`paintEvent` walks the cell grid and calls `blitGlyph` per cell, which takes the
glyph rows from `video_chip_->glyphRows()` so a program that has uploaded a soft
font gets its own shapes. A row flagged double-size is drawn at half the column
count. Fine vertical scroll is applied as a `y_offset` on each cell's destination
rectangle, with the clip scoped to the cell pass, so the later sprite pass does
not inherit it. Sprites are drawn after the cells with a transparent background,
and the cursor is painted last.

## Input

`DisplayWidget::keyPressEvent` maps the Qt key to a byte with `qtKeyToAscii` and
emits `keyPressed`, which `MainWindow` forwards to `getPia()->addKeypress`. That
is the keystroke buffer the kernel's `GET_KEYSTROKE` drains.

Held-key state is a separate path. `keyStateChanged` carries a bitmask to
`getPia()->setKeyState`, which backs the live control port at `$FE0F`. Games need
it because the keystroke buffer has no key-up event and so cannot express
move-while-fire.

## CPU register sidebar

Off by default, toggled from View, and intended for debugging rather than normal
use. It shows the current byte, A, X, Y, PC and SP, and the processor flags as
individual bits under an `NV-BDIZC` header. `updateCpuStatusSidebar` refreshes it
on the 100 ms timer.

`CPU6502::printStatus()` is an empty stub. The sidebar replaced it, and nothing
in the GUI writes register state to the console.
