/*
 *  png2art -- host developer tool: a PNG or GIF to a paste-ready @sprite block.
 *
 *  Run by hand while drawing, so a sprite can start from a picture. It is NOT part
 *  of any build: nothing in programs/catalog.txt or CMake runs it. The block it
 *  writes is the .art format spr2c reads (docs/video_design.md, "Drawing pictures").
 *
 *  Steps: trim the fully transparent margin; scale the rest to WxH by exact
 *  area-averaging in premultiplied alpha (so transparent pixels do not darken the
 *  edge); a pixel whose averaged alpha is below the threshold is '.', and any other
 *  is snapped to the nearest palette slot 1-F. Slot 0 is never produced for an
 *  opaque pixel: a bitmap sprite cannot draw it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#include "stb_image.h"

/* The 16 colours VIC::seedPalette() sets (src/computer/VIC.cpp). The test
   png2art_sprite compares this table with that file. */
static const unsigned char default_pal[16][3] = {
    {  0,   0,   0}, {170,   0,   0}, {  0, 255,   0}, {170,  85,   0},
    {  0,   0, 170}, {170,   0, 170}, {  0, 170, 170}, {170, 170, 170},
    { 85,  85,  85}, {255,  85,  85}, { 85, 255,  85}, {255, 255,  85},
    { 85,  85, 255}, {255,  85, 255}, { 85, 255, 255}, {255, 255, 255},
};

static unsigned char pal[16][3];

static void usage(void)
{
    fputs(
"usage: png2art -s WxH [options] IMAGE\n"
"  Turn a PNG or GIF into an @sprite block for an .art file (spr2c reads it).\n"
"  -s WxH        size in pixels (required): W even 2..128, H 1..128\n"
"  -n NAME       sprite name, a C identifier (default: sprite)\n"
"  -t THRESHOLD  alpha 0..255 below which a pixel is transparent '.' (default 110)\n"
"  -p FILE       palette file: lines of RRGGBB hex, 15 lines (slots 1-F) or 16\n"
"                (slot 0 first, ignored); blank and # lines skipped. Default is the\n"
"                16 colours VIC::seedPalette() sets\n"
"  -o FILE       write to FILE instead of standard output\n"
"  --dump-palette  print the palette in use as 16 RRGGBB lines and exit\n"
"The transparent margin is trimmed first. Slot 0 is never used for a colour.\n",
        stderr);
}

static void die(const char *a, const char *b)
{
    fprintf(stderr, "png2art: %s%s%s\n", a, b ? ": " : "", b ? b : "");
    exit(1);
}

static void load_palette(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[256];
    unsigned vals[16][3];
    int n = 0, i;
    if (!f) die("cannot open palette", path);
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        char *end;
        unsigned v;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == 0) continue;
        v = (unsigned)strtoul(p, &end, 16);
        if (end - p != 6) die("palette line is not RRGGBB", path);
        while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') end++;
        if (*end) die("palette line is not RRGGBB", path);
        if (n == 16) die("palette has more than 16 lines", path);
        vals[n][0] = v >> 16; vals[n][1] = (v >> 8) & 255; vals[n][2] = v & 255;
        n++;
    }
    fclose(f);
    if (n != 15 && n != 16) die("palette needs 15 or 16 lines", path);
    memcpy(pal, default_pal, sizeof pal);
    for (i = 0; i < n; i++) {
        int slot = (n == 16) ? i : i + 1;
        pal[slot][0] = (unsigned char)vals[i][0];
        pal[slot][1] = (unsigned char)vals[i][1];
        pal[slot][2] = (unsigned char)vals[i][2];
    }
}

int main(int argc, char **argv)
{
    const char *name = "sprite", *image = NULL, *palfile = NULL, *outname = NULL;
    int W = 0, H = 0, thresh = 110, dump = 0, i;
    unsigned char *img;
    int iw, ih, comp, x0, y0, x1, y1, x, y, dx, dy, bw, bh;
    double sx, sy;
    FILE *out;

    if (argc < 2) { usage(); return 1; }
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--dump-palette")) dump = 1;
        else if (!strcmp(a, "-s") && i + 1 < argc) {
            char *e;
            W = (int)strtol(argv[++i], &e, 10);
            if (*e != 'x' && *e != 'X') die("size must be WxH", argv[i]);
            H = (int)strtol(e + 1, &e, 10);
            if (*e) die("size must be WxH", argv[i]);
        }
        else if (!strcmp(a, "-n") && i + 1 < argc) name = argv[++i];
        else if (!strcmp(a, "-t") && i + 1 < argc) {
            char *e;
            thresh = (int)strtol(argv[++i], &e, 10);
            if (*e || thresh < 0 || thresh > 255) die("threshold must be 0..255", argv[i]);
        }
        else if (!strcmp(a, "-p") && i + 1 < argc) palfile = argv[++i];
        else if (!strcmp(a, "-o") && i + 1 < argc) outname = argv[++i];
        else if (a[0] == '-' && a[1]) { usage(); return 1; }
        else if (!image) image = a;
        else { usage(); return 1; }
    }
    memcpy(pal, default_pal, sizeof pal);
    if (palfile) load_palette(palfile);
    if (dump) {
        for (i = 0; i < 16; i++) printf("%02X%02X%02X\n", pal[i][0], pal[i][1], pal[i][2]);
        return 0;
    }
    if (!image || !W) { usage(); return 1; }
    if (W < 2 || W > 128 || (W & 1)) die("width must be even, 2..128", NULL);
    if (H < 1 || H > 128) die("height must be 1..128", NULL);
    if (!isalpha((unsigned char)name[0]) && name[0] != '_') die("name is not a C identifier", name);
    for (i = 0; name[i]; i++)
        if (!isalnum((unsigned char)name[i]) && name[i] != '_') die("name is not a C identifier", name);

    img = stbi_load(image, &iw, &ih, &comp, 4);
    if (!img) die(stbi_failure_reason() ? stbi_failure_reason() : "cannot read image", image);

    /* trim to the bounding box of every pixel with any alpha */
    x0 = iw; y0 = ih; x1 = -1; y1 = -1;
    for (y = 0; y < ih; y++)
        for (x = 0; x < iw; x++)
            if (img[(y * iw + x) * 4 + 3]) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    if (x1 < 0) die("image is entirely transparent", image);
    bw = x1 - x0 + 1; bh = y1 - y0 + 1;
    sx = (double)bw / W; sy = (double)bh / H;

    out = outname ? fopen(outname, "w") : stdout;
    if (!out) die("cannot write", outname);
    fprintf(out, "@sprite %s %dx%d\n", name, W, H);
    for (dy = 0; dy < H; dy++) {
        for (dx = 0; dx < W; dx++) {
            double ya = dy * sy, yb = (dy + 1) * sy;
            double xa = dx * sx, xb = (dx + 1) * sx;
            double sa = 0, sr = 0, sg = 0, sb = 0, area = (xb - xa) * (yb - ya);
            double bd = 1e30;
            int j, k, best = 1;
            for (j = (int)ya; j < bh && j < yb; j++) {
                double wy = (j + 1 < yb ? j + 1 : yb) - (j > ya ? j : ya);
                for (k = (int)xa; k < bw && k < xb; k++) {
                    double wx = (k + 1 < xb ? k + 1 : xb) - (k > xa ? k : xa);
                    const unsigned char *p = img + ((y0 + j) * iw + (x0 + k)) * 4;
                    double w = wx * wy * p[3];      /* premultiplied */
                    sa += w; sr += w * p[0]; sg += w * p[1]; sb += w * p[2];
                }
            }
            if (sa <= 0 || sa / area < thresh) { fputc('.', out); continue; }
            sr /= sa; sg /= sa; sb /= sa;           /* un-premultiply */
            for (k = 1; k < 16; k++) {
                double r = sr - pal[k][0], g = sg - pal[k][1], b = sb - pal[k][2];
                double d = r * r + g * g + b * b;
                if (d < bd) { bd = d; best = k; }
            }
            fputc("0123456789ABCDEF"[best], out);
        }
        fputc('\n', out);
    }
    if (outname) fclose(out);
    stbi_image_free(img);
    return 0;
}
