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
#include <math.h>

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
"  -b WxH        fit the picture in a WxH box centred in the -s slot, the rest '.'\n"
"                (box no larger than -s, width even; an odd height leaves the extra\n"
"                row below)\n"
"  -r DEGREES    rotate the full-size source clockwise by DEGREES (-360..360)\n"
"                before trimming and scaling\n"
"  -o FILE       write to FILE instead of standard output\n"
"  --dump-palette  print the palette in use as 16 RRGGBB lines and exit\n"
"The source is rotated first (-r), then the transparent margin is trimmed. Slot 0 is never used for a colour.\n",
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

/* A copy of img rotated clockwise by deg about its centre, on a canvas just big
   enough to hold it. Right angles are exact index moves; others sample bilinearly in
   premultiplied alpha, so a rotated edge blends with transparency and not with the
   black or white stored under it. Stored unpremultiplied again, as the rest expects. */
static unsigned char *rotate_image(const unsigned char *img, int iw, int ih,
                                   double deg, int *ow, int *oh)
{
    int x, y, nw, nh, quarter = -1;
    double a, c, s;
    unsigned char *out;
    deg = fmod(deg, 360.0);
    if (deg < 0) deg += 360.0;
    if (deg == 0 || deg == 90 || deg == 180 || deg == 270) quarter = (int)(deg / 90);
    a = deg * 3.14159265358979323846 / 180.0;
    c = cos(a); s = sin(a);
    if (quarter >= 0) { nw = (quarter & 1) ? ih : iw; nh = (quarter & 1) ? iw : ih; }
    else {
        nw = (int)ceil(iw * fabs(c) + ih * fabs(s) - 1e-9);
        nh = (int)ceil(iw * fabs(s) + ih * fabs(c) - 1e-9);
    }
    out = calloc((size_t)nw * nh, 4);
    if (!out) die("out of memory rotating the image", NULL);
    for (y = 0; y < nh; y++)
        for (x = 0; x < nw; x++) {
            unsigned char *d = out + ((size_t)y * nw + x) * 4;
            if (quarter >= 0) {
                int sx = x, sy = y;
                if (quarter == 1) { sx = y; sy = ih - 1 - x; }
                else if (quarter == 2) { sx = iw - 1 - x; sy = ih - 1 - y; }
                else if (quarter == 3) { sx = iw - 1 - y; sy = x; }
                memcpy(d, img + ((size_t)sy * iw + sx) * 4, 4);
            } else {
                double u = x + 0.5 - nw / 2.0, v = y + 0.5 - nh / 2.0;
                double fx = u * c + v * s + iw / 2.0 - 0.5;
                double fy = -u * s + v * c + ih / 2.0 - 0.5;
                int x0 = (int)floor(fx), y0 = (int)floor(fy), j, k;
                double tx = fx - x0, ty = fy - y0, sa = 0, sr = 0, sg = 0, sb = 0;
                for (j = 0; j < 2; j++)
                    for (k = 0; k < 2; k++) {
                        int px = x0 + k, py = y0 + j;
                        double w = (k ? tx : 1 - tx) * (j ? ty : 1 - ty);
                        const unsigned char *p;
                        if (px < 0 || py < 0 || px >= iw || py >= ih) continue;
                        p = img + ((size_t)py * iw + px) * 4;
                        w *= p[3];
                        sa += w; sr += w * p[0]; sg += w * p[1]; sb += w * p[2];
                    }
                if (sa >= 0.5) {
                    d[0] = (unsigned char)(sr / sa + 0.5);
                    d[1] = (unsigned char)(sg / sa + 0.5);
                    d[2] = (unsigned char)(sb / sa + 0.5);
                    d[3] = (unsigned char)(sa + 0.5);
                }
            }
        }
    *ow = nw; *oh = nh;
    return out;
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
    int have_box = 0, W = 0, H = 0, BW = 0, BH = 0, ox, oy, rotate = 0, thresh = 110, dump = 0, i;
    unsigned char *img;
    int iw, ih, comp, x0, y0, x1, y1, x, y, dx, dy, bw, bh;
    double sx, sy, deg = 0;
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
        else if (!strcmp(a, "-b") && i + 1 < argc) {
            const char *e;
            long w, h;
            w = parse_num(argv[++i], &e, 0, 100000);
            if (w < 0 || (*e != 'x' && *e != 'X')) die("box must be WxH", argv[i]);
            h = parse_num(e + 1, &e, 0, 100000);
            if (h < 0 || *e) die("box must be WxH", argv[i]);
            BW = (int)w; BH = (int)h; have_box = 1;
        }
        else if (!strcmp(a, "-r") && i + 1 < argc) {
            char *e;
            const char *t = argv[++i];
            const char *d = (*t == '-' || *t == '+') ? t + 1 : t;
            if (!isdigit((unsigned char)*d)) die("rotation must be degrees, -360..360", t);
            deg = strtod(t, &e);
            if (*e || !(deg >= -360 && deg <= 360)) die("rotation must be degrees, -360..360", t);
            rotate = 1;
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
    if (have_box) {
        if (BW < 2 || (BW & 1) || BH < 1) die("box width must be even and at least 2, height at least 1", NULL);
        if (BW > W || BH > H) die("box is larger than the -s size", NULL);
    } else { BW = W; BH = H; }
    ox = (W - BW) / 2; oy = (H - BH) / 2;
    if (!isalpha((unsigned char)name[0]) && name[0] != '_') die("name is not a C identifier", name);
    for (i = 0; name[i]; i++)
        if (!isalnum((unsigned char)name[i]) && name[i] != '_') die("name is not a C identifier", name);
    if (strlen(name) > 63) die("name is longer than 63 characters (spr2c's limit)", NULL);
    for (i = 0; c_keywords[i]; i++)
        if (!strcmp(name, c_keywords[i])) die("name is a C keyword", name);

    img = stbi_load(image, &iw, &ih, &comp, 4);
    if (!img) die(stbi_failure_reason() ? stbi_failure_reason() : "cannot read image", image);

    if (rotate) {
        unsigned char *r = rotate_image(img, iw, ih, deg, &iw, &ih);
        stbi_image_free(img);
        img = r;
    }

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
    sx = (double)bw / BW; sy = (double)bh / BH;

    out = outname ? fopen(outname, "w") : stdout;
    if (!out) die("cannot write", outname);
    fprintf(out, "@sprite %s %dx%d\n", name, W, H);
    for (dy = 0; dy < H; dy++) {
        for (dx = 0; dx < W; dx++) {
            int bx = dx - ox, by = dy - oy;
            double ya = by * sy, yb = (by + 1) * sy;
            double xa = bx * sx, xb = (bx + 1) * sx;
            double sa = 0, sr = 0, sg = 0, sb = 0, area = (xb - xa) * (yb - ya);
            double bd = 1e30;
            int j, k, best = 1;
            if (bx < 0 || by < 0 || bx >= BW || by >= BH) { fputc('.', out); continue; }
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
