# GUI mockups

Static mockups of the optional GUI described in [`docs/gui_os_design.md`](../../gui_os_design.md).
They are illustrations, not screenshots: no GUI code exists yet. Every image is shown at 2x
(1280x800), except the close-ups.

## How they were drawn

Each picture is rendered under the display rules the design chose, so the hardware could show
every pixel in it:

- a 640x400 plane at 1 bit a pixel (the glyph-tiled framebuffer, the design's option A);
- two colours per 8x16 cell, taken from the cell's attribute: one colour from any of the 16
  palette slots and the other from slots 0-7 (foreground 0-15 with bright, background 0-7,
  with reverse swapping them). The renderer refuses a cell that breaks this rule;
- the default palette from `VIC::seedPalette()`;
- the pointer as sprite 16, a 16x16 bitmap sprite at 4 bits a pixel. Slot 0 is
  transparent, so the pointer has no black.

Text uses a proportional font cut from the machine's CP437 VGA font
(`vendor/cp437font/VGA8.F16`). It stands in for the new font the design calls for. Most
program names come from `programs/catalog.txt`. HELLO is the example program from question 6.
SST.BAS and README.TXT are sample files. CHESS, KPANIC and VENTURE are drawn at the disk root,
though the catalog installs them under `GAMES/`. The clock, the free space and the item count
are made up.

## What the GUI would look like

| Image | Shows |
|---|---|
| `1-desktop.png` | The menu bar with the RTC clock; the disk window with program icons (KPANIC selected); the disk and Waste icons on the patterned desktop |
| `2-file-menu.png` | The File menu, with **Open** highlighted, **Delete** greyed out and keyboard shortcuts. Each item takes one 16-pixel cell row, so the highlight covers exactly one item |
| `3-open-dialog.png` | An Open dialog with a scrolling file list, a highlighted selection and buttons |
| `4-hello-window.png` | The "hello window" program from question 6: 208x112 at `GUI_CENTRE` (216, 144), on the cell grid |
| `5a-cells-aligned.png` | A close-up of image 4 with the 8x16 cell grid in magenta. Each colour boundary lies on a cell edge |

## Failure modes found in review

These images show the problems the PR #8 review found in the design. The corrections in PR #9
(bead `bd_6502kern-0aj`) fixed them in the design, so they record what the rules prevent.
They do not show the current design.

| Image | Review item | Shows |
|---|---|---|
| `5b-cells-unaligned-clash.png` | 6, cell grid | A window placed off the grid at (220, 152). The desktop pattern bleeds into the window's edge cells and the frame changes colour partway along |
| `6-raster-jitter.png`, `6b-raster-jitter-labels.png`, `6c-raster-jitter-titlebar.png` | 7, raster jitter | Font-set switches landing 0-6 lines late. A late line takes its glyphs from the previous band's set, which holds the picture 48 pixels higher. That cuts off the tops of labels and drops menu-bar fragments into a title bar. The lateness values are illustrative, not measured |
| `7-stop-monitor.png` | 3, STOP | Breaking into the monitor with the GUI's font still live. The cells show the font's unused codes, the monitor's prompt comes out as GUI pixels, and the pointer sprite stays on screen |
| `8-quit-raw-clear.png` | 2, quit | Quitting with a raw `kCmdClear`. Every cell holds glyph 8, the raster handler's last font-set number, and `]` lands one row below the old cursor, because `DOS_WARM` prints a newline first. The attribute colours are an assumption |
| `9-highlight-straddle.png`, `9b-highlight-straddle-zoom.png` | 6, cell grid | Menu items 4 pixels off the cell rows. Highlighting **Open** colours whole cells, so **Info** looks selected too |
