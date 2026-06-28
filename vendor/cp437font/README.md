# CP437 character ROM (IBM VGA 8×16)

`VGA8.F16` is the **IBM VGA 8×16 text-mode font** in the VGA BIOS's native raw
format: 256 glyphs, 16 bytes each (one byte per scanline, bit 7 = leftmost
pixel) = 4096 bytes. It is the character generator behind the MFC computer's
80×25 screen, giving the full CP437 set (box-drawing, blocks, accented, symbols)
to every program.

## Provenance & licensing

The font is the original IBM VGA ROM bitmap. **Raw bitmap font data is public
domain** — U.S. copyright does not protect typeface bitmaps (only scalable
outline *programs*), which is why this font is embedded across the free-software
world. This particular dump was obtained from **VileR's `vga-text-mode-fonts`**
collection (<https://int10h.org/>, <https://github.com/viler-int10h/vga-text-mode-fonts>),
whose curation we gratefully acknowledge. Original font © IBM (ROM bitmap, public
domain).

## How it's used

`make_font.py` converts `VGA8.F16` into `include/computer/Cp437Font.h` (a
`kCp437Font[256*16]` array) which `src/ui/DisplayWidget.cpp` blits per cell. This
mirrors the project's other vendored-source pattern (see `vendor/fig-forth`,
`vendor/xmodem`): the pristine source plus a generator, with the generated file
checked in.

```
python3 vendor/cp437font/make_font.py   # VGA8.F16 -> include/computer/Cp437Font.h
```
