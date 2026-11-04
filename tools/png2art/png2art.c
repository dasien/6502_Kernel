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
#include <errno.h>

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
    int n = 0, i, lineno = 0;
    if (!f) die("cannot open palette", path);
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        char *end;
        unsigned v = 0;
        size_t len = strlen(line);
        lineno++;
        if (len == sizeof line - 1 && line[len - 1] != '\n' && !feof(f)) {
            fprintf(stderr, "png2art: palette %s: line %d is too long\n", path, lineno);
            exit(1);
        }
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == 0) continue;
        /* exactly six hex digits: strtoul would also take 0x, + and - */
        for (end = p; end - p < 6 && isxdigit((unsigned char)*end); end++)
            v = v * 16 + (unsigned)(isdigit((unsigned char)*end) ? *end - '0'
                                    : (tolower((unsigned char)*end) - 'a' + 10));
        while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') end++;
        if (end - p < 6 || *end || isxdigit((unsigned char)p[6])) {
            fprintf(stderr, "png2art: palette %s: line %d is not RRGGBB\n", path, lineno);
            exit(1);
        }
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

/* the whole of s as a decimal integer in lo..hi, else -1 */
static long parse_num(const char *s, const char **end, long lo, long hi)
{
    char *e;
    long v;
    if (!isdigit((unsigned char)*s)) return -1;
    errno = 0;
    v = strtol(s, &e, 10);
    *end = e;
    if (errno || v < lo || v > hi) return -1;
    return v;
}

static const char *const c_keywords[] = {
    "auto", "break", "case", "char", "const", "continue", "default", "do", "double",
    "else", "enum", "extern", "float", "for", "goto", "if", "inline", "int", "long",
    "register", "restrict", "return", "short", "signed", "sizeof", "static", "struct",
    "switch", "typedef", "union", "unsigned", "void", "volatile", "while", "_Bool",
    "_Complex", "_Imaginary", "_Alignas", "_Alignof", "_Atomic", "_Generic",
    "_Noreturn", "_Static_assert", "_Thread_local", NULL
};

static void finish_output(FILE *out, const char *outname)
{
    int bad = fflush(out) != 0 || ferror(out);
    if (outname) bad |= fclose(out) != 0;
    if (bad) die("cannot write", outname ? outname : "standard output");
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
            const char *e;
            long w, h;
            w = parse_num(argv[++i], &e, 0, 100000);
            if (w < 0 || (*e != 'x' && *e != 'X')) die("size must be WxH", argv[i]);
            h = parse_num(e + 1, &e, 0, 100000);
            if (h < 0 || *e) die("size must be WxH", argv[i]);
            W = (int)w; H = (int)h;
        }
        else if (!strcmp(a, "-n") && i + 1 < argc) name = argv[++i];
        else if (!strcmp(a, "-t") && i + 1 < argc) {
            const char *e;
            long t = parse_num(argv[++i], &e, 0, 255);
            if (t < 0 || *e) die("threshold must be 0..255", argv[i]);
            thresh = (int)t;
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
        finish_output(stdout, NULL);
        return 0;
    }
    if (!image || !W) { usage(); return 1; }
    if (W < 2 || W > 128 || (W & 1)) die("width must be even, 2..128", NULL);
    if (H < 1 || H > 128) die("height must be 1..128", NULL);
    if (!isalpha((unsigned char)name[0]) && name[0] != '_') die("name is not a C identifier", name);
    for (i = 0; name[i]; i++)
        if (!isalnum((unsigned char)name[i]) && name[i] != '_') die("name is not a C identifier", name);
    if (strlen(name) > 63) die("name is longer than 63 characters (spr2c's limit)", NULL);
    for (i = 0; c_keywords[i]; i++)
        if (!strcmp(name, c_keywords[i])) die("name is a C keyword", name);

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
            /* the box weights are fractions, so sa can sum an ulp under thresh * area
               when the average is exactly the threshold: allow for it */
            if (sa <= 0 || sa < thresh * area * (1 - 1e-9)) { fputc('.', out); continue; }
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
    stbi_image_free(img);
    finish_output(out, outname);
    return 0;
}
