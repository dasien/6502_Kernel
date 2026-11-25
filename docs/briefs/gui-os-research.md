# Brief: research an optional graphical OS for MFC

**Kind:** research spike. The deliverable is a design document, not code.
**Reference:** [mist64/geos](https://github.com/mist64/geos), the reconstructed source of
GEOS 2.0 for the Commodore 64. It is both a guide to how GEOS is built and a source that
may be adapted, with credit (see Constraints).

## Problem

MFC boots into a text shell (MFC/OS, the `]` prompt) on an 80x25 CP437 character display.
GEOS showed that an 8-bit, 64 KB machine can run a usable windowed desktop with menus,
dialogs, proportional fonts and a file manager. We want to know whether MFC can have one
too, what it would cost, and what it should look like, before anyone writes code.

## Desired outcome

A design document, `docs/gui_os_design.md`, that a later planning pass can split into
implementation beads. It works the way the MFC-DOS design document did (now part of `docs/dos_internals.md`). It
recommends one approach, gives the reasons, and names the alternatives it rejected.

## "Optional" means

- **The machine still boots to DOS.** The GUI runs on top of DOS, the way Windows 3.1 ran
  on MS-DOS. Nothing about the boot sequence changes.
- You start the GUI with a DOS command at the `]` prompt, as you would type `WIN` under
  MS-DOS. The document proposes the command's name. To have it start at every boot, add
  that command to `SYSTEM/STARTUP.CFG`, as `AUTOEXEC.BAT` did for Windows. DOS itself
  never starts it automatically.
- The leading candidate is a **module in bank 2**, the free 16 KB window at
  `$B000-$EFFF`. The document should confirm this or say why it won't fit. The other
  options are a `.PRG` in user RAM (`$0800-$87FF`) or a mix of the two.
- Quitting the GUI returns to the `]` prompt with DOS state intact, as exiting Windows
  dropped you back to `C:\>`.
- The GUI uses DOS for files and program loading. It does not replace DOS. Question 5
  covers how.

## Questions the document must answer

1. **Display model.** GEOS draws on a 320x200 bitmap. MFC has no bitmap mode (see
   `docs/video_design.md`, "What is still missing"). Compare at least these three:
   - **Glyph-tiled framebuffer.** Give each cell of a region its own glyph code and draw
     into font RAM. One 256-glyph font gives 128x256 pixels; there are 16 fonts.
   - **Character-cell GUI.** Windows, menus and dialogs built from CP437 box-drawing
     characters and the soft font. There are no pixel-level graphics.
   - **A new VIC bitmap mode.** This is a C++ chipset change. Cover the cost: VIC
     registers, the I/O budget (first free byte `$FED5`, 43 bytes left), the renderer
     and its tests.

   For each one, cover resolution, colour, how fast it redraws through the register
   port, memory cost and how it renders text.
2. **Pointer and input.** GEOS assumes a mouse or joystick, and MFC has neither. Cover
   driving a pointer from the keyboard and the held-key port at `$FE0F`. Also cover
   adding a mouse device to the chipset, which means host-side Qt input plus registers.
   Recommend one. Using a sprite as the pointer looks likely, since there are 17
   sprites and bitmap sprite patterns are available.
3. **Architecture from GEOS.** What to adopt, adapt or drop:
   - the main loop and event dispatch
   - menus and dialog boxes, and how GEOS describes them as data tables
   - the graphics primitives (lines, rectangles, patterns)
   - proportional fonts
   - process and timer handling
   - the desktop and file manager
   - the memory layout, set against MFC's real map in `docs/architecture.md`
4. **Kernel ABI.** List every existing `$FF00` routine the GUI will use. Then list each
   proposed new entry (name, inputs, outputs, ROM cost) and the reason it belongs in the
   kernel and not in the module. Say which primitives stay private to the module, and why.
5. **Filesystem integration.** How the desktop presents FAT16 (directories, icons, launching
   `.PRG` programs, returning from them) through the existing DOS calls.
6. **Application model.** What a GUI application is (a `.PRG` with a header? a cc65
   library?), and what a minimal "hello window" program looks like.
7. **Phasing.** An ordered list of vertical slices, each one demonstrable on its own. For
   example: a pointer moves on screen, then a menu bar, then a dialog box, then a
   desktop listing a directory, then launching a program. Give each slice a rough size.

## Constraints

- **GEOS code may be adapted, with credit**, the same way this repository already treats
  its vendored ports (`vendor/`, README "Acknowledgments"). Background: the 8-bit GEOS
  rights holders released it as freeware in 2003 (Apple II) and 2004 (C64/128). The
  reverse-engineers' own work in mist64/geos is public domain, and they ask to be
  credited. Freeware status allowed free distribution, but no formal open-source licence
  was ever published. So:
  - Credit Berkeley Softworks and the reverse-engineers (Maciej Witkowiak, Michael Steil)
    in README "Acknowledgments", and note that the 8-bit GEOS freeware release is the
    basis for the reuse.
  - Keep the original source of any routine that is adapted under `vendor/geos/`, with its
    README notice, so the derivation is traceable.
  - Most routines will need rewriting regardless, because they assume the VIC-II bitmap and
    C64 addresses. The document should say, for each part it proposes to reuse, whether
    it is a direct adaptation or a rewrite.
  - [FreeGEOS](https://github.com/bluewaysw/pcgeos) (PC/GEOS, Apache 2.0) is a different,
    x86 codebase. It is a design reference only, not a source of 6502 code.
- **Not a C64 clone.** MFC has its own memory map and chipset: no VIC-II, no CIA, no
  `$00/$01` port, no PETSCII. The design targets MFC's VIC register port and the WDC
  65C02.
- **Respect the memory map** in `docs/architecture.md`. User RAM ends at `$87FF`. Banking
  touches only `$B000-$EFFF`, and the previous bank is restored on exit.
- **Build on the kernel.** The GUI calls the existing `$FF00` kernel routines wherever one
  fits: character and string output, VIC access, keyboard, sound, the jiffy timer, bank
  switching and the DOS file calls. It does not keep private copies of them. Where the GUI
  needs a primitive the kernel lacks and another program could use it too (pointer state,
  rectangles, a glyph-region blit, an event queue), the document proposes it as a new
  kernel ABI entry. Each proposal gives the entry's name, inputs and outputs, and states
  whether it fits in the kernel ROM's free space. When a related set is promoted, the
  whole set moves together.

## Out of scope

- Any implementation, including a prototype. A throwaway measurement (for example, timing
  how long a full glyph-region redraw takes through the port) is allowed if its numbers
  appear in the document and the code does not merge.
- Replacing DOS or changing the boot sequence.
- Networking, printing or the GEOS application suite (geoWrite, geoPaint and the rest).

## Acceptance

- `docs/gui_os_design.md` exists and answers questions 1-7, each under its own heading.
- The document names the DOS command that starts the GUI, and explains how it returns to
  the `]` prompt and how `SYSTEM/STARTUP.CFG` can start it at boot.
- The display-model section has a comparison table covering the three options, with
  numbers for resolution, colours, memory and redraw time. It ends with one
  recommendation.
- Every MFC address, register or ABI entry the document cites exists in
  `docs/architecture.md`, `docs/board.md` or `docs/video_design.md`, or is clearly marked
  as a proposed addition.
- The kernel ABI section names the existing `$FF00` routines the GUI uses, and gives each
  proposed new entry with its inputs, outputs and ROM cost.
- The phasing section lists at least four vertical slices, in order, each with a
  demonstrable result.
- The document says, for each part of GEOS it proposes to use, whether that part is
  adapted code or a rewrite. Every part marked as adapted names the original file in
  mist64/geos it comes from.
- No file under `src/`, `programs/` or `tools/` changes.
