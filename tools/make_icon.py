#!/usr/bin/env python3
"""Generate the MFC 6502 application icon.

The mark is the machine's boot prompt: a ']' followed by the block cursor, in
bright green on black -- the first thing MFC puts on screen at power-on.

The glyphs are not redrawn. They are read out of include/computer/Cp437Font.h,
the same 8x16 character ROM the VIC renders text with, so the logo is literally
set in the computer's own typeface and cannot drift from it. Two 8x16 cells side
by side come to exactly 16x16, which means at icon size the mark is the machine's
glyphs at 1:1 with no scaling or hinting at all.

Colours come from the emulator's palette in src/ui/DisplayWidget.cpp:
bright green (attribute 10) on black (attribute 0).

Usage:  python3 tools/make_icon.py [outdir]     (default: assets/)
"""

import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

REPO = Path(__file__).resolve().parent.parent
FONT_H = REPO / "include" / "computer" / "Cp437Font.h"

GLYPH_BRACKET = 0x5D          # ']'
GLYPH_CURSOR = 0xDB           # full block
FG = (85, 255, 85)            # palette_[10], bright green
BG = (0, 0, 0)                # palette_[0], black
GLOW = (85, 255, 85)

# The bracket glyph's ink sits on rows 2..11 of its 16-row cell. The cursor is
# trimmed to that same band so the pair share a baseline, and nudged down a row so
# the mark is optically centred rather than sitting on the font's baseline (the
# cell reserves rows below for descenders that ']' does not use).
INK_TOP, INK_BOTTOM = 2, 11
BASELINE_NUDGE = 1

# MFC draws a full-cell block cursor, and at 8 px wide it is a solid slab twice
# the visual weight of the bracket -- the mark stopped reading as a prompt and
# started reading as "a bracket next to a green square". Narrowing it to a 5 px
# bar with a column of air matches the bracket's stroke weight, and both survive
# at 16x16, where an underline cursor thins out to almost nothing.
CURSOR_WIDTH = 5
CURSOR_GAP = 1

SIZES = [16, 32, 48, 64, 128, 256, 512]
ICO_SIZES = [16, 32, 48, 64, 128, 256]


def load_font():
    """Parse the byte table out of the generated C header."""
    text = FONT_H.read_text()
    data = [int(b, 16) for b in re.findall(r"0x([0-9A-Fa-f]{2})\s*,", text)]
    if len(data) < 256 * 16:
        raise SystemExit(f"{FONT_H}: expected 4096 font bytes, found {len(data)}")
    return data


def build_art(font):
    """The 16x16 pixel mark: ']' plus the cursor, as lit (x, y) pixels."""
    lit = set()

    # Cell 0: the ']' exactly as the character ROM has it.
    for row in range(16):
        bits = font[GLYPH_BRACKET * 16 + row]
        y = row + BASELINE_NUDGE
        if y > 15:
            continue
        for bit in range(8):
            if bits & (0x80 >> bit):
                lit.add((bit, y))

    # Cell 1: the cursor, narrowed and inset (see CURSOR_WIDTH above).
    x0 = 8 + CURSOR_GAP
    for row in range(INK_TOP, INK_BOTTOM + 1):
        y = row + BASELINE_NUDGE
        if y > 15:
            continue
        for bit in range(CURSOR_WIDTH):
            if x0 + bit < 16:
                lit.add((x0 + bit, y))
    return lit


def render(lit, size, *, glow=True, rounded=True):
    """Draw the mark at `size` px, keeping the pixel grid crisp."""
    # Integer scale so glyph pixels stay square and sharp; leave a margin at the
    # larger sizes so the mark is not jammed into the corners of the tile.
    scale = max(1, int(size * (1.0 if size <= 16 else 0.78)) // 16)
    art = scale * 16
    off = (size - art) // 2

    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    radius = 0 if size <= 16 else max(2, size // 6)
    if rounded and radius:
        draw.rounded_rectangle([0, 0, size - 1, size - 1], radius=radius, fill=BG + (255,))
    else:
        draw.rectangle([0, 0, size - 1, size - 1], fill=BG + (255,))

    glyphs = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glyphs)
    for (x, y) in lit:
        x0, y0 = off + x * scale, off + y * scale
        gd.rectangle([x0, y0, x0 + scale - 1, y0 + scale - 1], fill=FG + (255,))

    # Phosphor bloom: a soft halo, the way green text blooms on a CRT. Skipped at
    # small sizes where it would just muddy the two glyphs.
    if glow and size >= 48:
        halo = glyphs.filter(ImageFilter.GaussianBlur(radius=max(1, scale)))
        halo = Image.blend(Image.new("RGBA", (size, size), (0, 0, 0, 0)), halo, 0.55)
        img.alpha_composite(halo)
    img.alpha_composite(glyphs)
    return img


def main():
    outdir = Path(sys.argv[1]) if len(sys.argv) > 1 else REPO / "assets"
    outdir.mkdir(parents=True, exist_ok=True)

    lit = build_art(load_font())
    for size in SIZES:
        img = render(lit, size)
        img.save(outdir / f"mfc6502-{size}.png")
        print(f"  {outdir.name}/mfc6502-{size}.png")

    base = render(lit, 256)
    base.save(outdir / "mfc6502.ico",
              sizes=[(s, s) for s in ICO_SIZES])
    print(f"  {outdir.name}/mfc6502.ico  ({len(ICO_SIZES)} sizes)")

    # A flat 1:1 version with no rounding or bloom, for embedding in docs.
    render(lit, 256, glow=False, rounded=False).save(outdir / "mfc6502-flat.png")
    print(f"  {outdir.name}/mfc6502-flat.png")

    write_header(outdir / "mfc6502-256.png")


def write_header(png):
    """Emit the icon as a C array, the way Cp437Font.h carries the character ROM.

    Qt's resource system would work too, but the app already has trouble with
    relative paths (it must be launched from bin/ for the ROMs to load) and an
    icon that silently fails to appear is a poor trade for avoiding one generated
    header. A byte array cannot go missing at runtime.
    """
    data = png.read_bytes()
    out = REPO / "include" / "ui" / "AppIcon.h"
    rows = []
    for i in range(0, len(data), 16):
        rows.append("    " + " ".join(f"0x{b:02X}," for b in data[i:i + 16]))
    out.write_text(
        "// AppIcon.h - GENERATED by tools/make_icon.py; do not edit.\n"
        "// The MFC 6502 application icon: the boot prompt ']' and its cursor, set\n"
        "// in the machine's own CP437 character ROM. Regenerate with:\n"
        "//     python3 tools/make_icon.py\n"
        "#ifndef MFC_APPICON_H\n"
        "#define MFC_APPICON_H\n\n"
        "#include <cstdint>\n\n"
        "namespace Ui\n{\n"
        f"    // 256x256 PNG, {len(data)} bytes.\n"
        "    static const uint8_t kAppIconPng[] = {\n"
        + "\n".join(rows) +
        "\n    };\n"
        f"    static const unsigned kAppIconPngLen = {len(data)};\n"
        "}\n\n#endif // MFC_APPICON_H\n")
    print(f"  include/ui/AppIcon.h  ({len(data)} bytes embedded)")


if __name__ == "__main__":
    main()
