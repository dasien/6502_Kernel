/*
 *  spr2c -- host build tool: sprite and glyph art, drawn as text, to C arrays.
 *
 *  A program's pictures live in an .art file as pictures, one character a pixel,
 *  and this turns them into the packed bytes the VIC wants at build time. The
 *  drawing is the source; the bytes are generated and can never drift from it.
 *
 *  Usage:  spr2c <in.art> [out.c]      (stdout when out.c is omitted)
 *
 *  The .art format -- directives, rows, the byte layout and the errors -- is
 *  specified in docs/video_design.md, under "Drawing pictures". In brief:
 *  `@sprite NAME WxH` then H rows of '.' and hex 1-F (4 bits a pixel), or
 *  `@glyph NAME 8xH` then H rows of '.' and '#' (1 bit a pixel), and '#' comment
 *  lines between pictures. Keep that section and this code in step: the test
 *  spr2c_art pins the layout it describes.
 *
 *  Built by CMake as a host tool, like dat2c; programs/catalog.txt runs it for any
 *  entry with `generate = NAME.art -> NAME.c`.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_W     128           /* the widest sprite the chip composes: 8 slots */
#define MAX_H     128
#define MAX_NAMES 256

static const char *in_name;
static int line_no;
static char tmp[1024];                  /* the output, until it is complete */

static void fail(const char *msg, const char *detail)
{
    fprintf(stderr, "%s:%d: %s%s%s\n", in_name, line_no, msg,
            detail ? ": " : "", detail ? detail : "");
    if (tmp[0]) remove(tmp);
    exit(1);
}

/* One picture being read. */
static int  kind;                       /* 0 none, 1 sprite, 2 glyph */
static char name[64];
static int  w, h, rows_read;
static char pix[MAX_H][MAX_W + 1];

static char seen[MAX_NAMES][64];
static int  nseen;

static int nib(char c)
{
    if (c == '.') return 0;
    if (c >= '1' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static void emit(FILE *out)
{
    unsigned char bytes[MAX_W * MAX_H / 2];
    int n = 0, x, y, sx, sy, r;

    if (rows_read != h) {
        char d[80];
        snprintf(d, sizeof d, "%s has %d rows, its header says %d", name, rows_read, h);
        fail("picture ends early", d);
    }

    if (kind == 2) {
        for (y = 0; y < h; y++) {
            unsigned char b = 0;
            for (x = 0; x < 8; x++) if (pix[y][x] == '#') b |= (unsigned char)(0x80 >> x);
            bytes[n++] = b;
        }
    } else if (w % 16 == 0 && h % 16 == 0) {
        for (sy = 0; sy < h / 16; sy++)
            for (sx = 0; sx < w / 16; sx++)
                for (r = 0; r < 16; r++)
                    for (x = 0; x < 16; x += 2) {
                        const char *row = pix[sy * 16 + r] + sx * 16;
                        bytes[n++] = (unsigned char)((nib(row[x]) << 4) | nib(row[x + 1]));
                    }
    } else {
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x += 2)
                bytes[n++] = (unsigned char)((nib(pix[y][x]) << 4) | nib(pix[y][x + 1]));
    }

    fprintf(out, "/*\n *   %s, %dx%d%s\n", name, w, h, kind == 2 ? ", glyph" : "");
    for (y = 0; y < h; y++) fprintf(out, " *   %s\n", pix[y]);
    fprintf(out, " */\nconst unsigned char %s[%d] = {\n", name, n);
    for (x = 0; x < n; x++)
        fprintf(out, "%s0x%02X,%s", x % 16 ? " " : "    ", bytes[x],
                (x % 16 == 15 || x == n - 1) ? "\n" : "");
    fprintf(out, "};\n\n");
    kind = 0;
}

static void start(char *line, FILE *out)
{
    char dir[16], nm[64];
    int i, ww, hh;

    if (kind) emit(out);
    if (sscanf(line, "@%15s %63s %dx%d", dir, nm, &ww, &hh) != 4)
        fail("expected '@sprite NAME WxH' or '@glyph NAME 8xH'", line);
    for (i = 0; nm[i]; i++)
        if (!(isalnum((unsigned char)nm[i]) || nm[i] == '_') || (i == 0 && isdigit((unsigned char)nm[i])))
            fail("not a C identifier", nm);
    for (i = 0; i < nseen; i++) if (!strcmp(seen[i], nm)) fail("name used twice", nm);
    if (nseen == MAX_NAMES) fail("too many pictures", NULL);
    strcpy(seen[nseen++], nm);

    if (!strcmp(dir, "sprite")) {
        if (ww < 2 || ww > MAX_W || ww % 2) fail("sprite width must be even, 2..128", nm);
        if (hh < 1 || hh > MAX_H) fail("sprite height must be 1..128", nm);
        kind = 1;
    } else if (!strcmp(dir, "glyph")) {
        if (ww != 8) fail("a glyph is 8 pixels wide", nm);
        if (hh < 1 || hh > 32) fail("glyph height must be 1..32", nm);
        kind = 2;
    } else {
        fail("unknown directive", dir);
    }
    strcpy(name, nm);
    w = ww; h = hh; rows_read = 0;
}

static void row(char *line)
{
    int x, len = (int)strlen(line);
    char d[96];

    if (!kind) fail("a picture row outside any @sprite or @glyph", line);
    if (rows_read == h) {
        snprintf(d, sizeof d, "%s is %d rows, this is one more", name, h);
        fail("too many rows", d);
    }
    if (len != w) {
        snprintf(d, sizeof d, "%s is %d wide, this row is %d", name, w, len);
        fail("wrong row width", d);
    }
    for (x = 0; x < len; x++) {
        const char c = line[x];
        const int ok = (kind == 2) ? (c == '.' || c == '#') : (nib(c) >= 0);
        if (!ok) {
            snprintf(d, sizeof d, "'%c' in column %d of %s", c, x + 1, name);
            fail(kind == 2 ? "a glyph row is '.' and '#' only"
                           : "a sprite row is '.' and hex 1-F only", d);
        }
    }
    strcpy(pix[rows_read++], line);
}

int main(int argc, char **argv)
{
    FILE *in, *out = stdout;
    char buf[512];

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: spr2c <in.art> [out.c]\n");
        return 2;
    }
    in_name = argv[1];
    in = fopen(in_name, "r");
    if (!in) { perror(in_name); return 1; }

    /* Write to a temporary name and rename at the end, so a failed run never
       leaves a half-written file that the build would then treat as current. */
    if (argc == 3) {
        snprintf(tmp, sizeof tmp, "%s.tmp", argv[2]);
        out = fopen(tmp, "w");
        if (!out) { perror(tmp); return 1; }
    }

    {   /* the file's own name, not the build machine's path to it */
        const char *base = strrchr(in_name, '/');
        fprintf(out, "/* GENERATED by spr2c from %s -- edit that, not this. */\n\n",
                base ? base + 1 : in_name);
    }

    while (fgets(buf, sizeof buf, in)) {
        size_t n = strlen(buf);
        line_no++;
        while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r' ||
                     buf[n - 1] == ' ' || buf[n - 1] == '\t')) buf[--n] = 0;
        if (n == 0) continue;
        /* Rows first: inside a picture a '#' row is ink, not a comment. A new
           header arriving early is left to start(), which reports the short one. */
        if (kind && rows_read < h && buf[0] != '@') { row(buf); continue; }
        if (buf[0] == '#') continue;
        if (buf[0] == '@') start(buf, out);
        else               row(buf);
    }
    line_no++;
    if (kind) emit(out);
    if (!nseen) fail("no pictures in the file", NULL);
    fclose(in);

    if (argc == 3) {
        if (fclose(out) != 0 || rename(tmp, argv[2]) != 0) { perror(argv[2]); return 1; }
    }
    return 0;
}
