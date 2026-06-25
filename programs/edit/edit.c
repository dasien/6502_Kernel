/*
 *  EDIT -- a minimal full-screen text editor for MFC-DOS (kilo-inspired).
 *
 *  Renders directly to the 40x25 screen RAM at $0400 (no ANSI/terminal), reads
 *  keys via the kernel (GET_KEYSTROKE, case-preserving), and edits a document
 *  held as dynamically-allocated lines (kilo's erow model): a fixed array of
 *  row descriptors, each pointing at a malloc'd string sized to its content, so
 *  capacity is bounded by total text (the heap) rather than a per-line width or
 *  a small row count. Vertical + horizontal scroll, a reverse-video block
 *  cursor and status line, and DOS-FS load/save (Ctrl-O / Ctrl-S).
 *
 *  ESC quits to the DOS prompt (a deliberate quit + unsaved guard is a TODO).
 */

#include <string.h>
#include <stdlib.h>

#define COLS      40
#define TEXTROWS  24            /* rows 0..23 are text; row 24 is the status line */
#define MAXROWS   600           /* line-descriptor slots (text itself is on the heap) */

/* ---- kernel hooks (glue.s) ---- */
char INCH(void);                /* blocking: returns the next key, true case */
int  INCH_NB(void);             /* non-blocking: next key 0..255, or -1 if none */
void QUITDOS(void);             /* return to the DOS ] prompt */
char dopen_read(char *name);    /* DOS file I/O: 0 = ok, 1 = error */
char dopen_write(char *name);
int  dgetb(void);               /* next byte 0..255, or -1 at EOF */
char dputb(char c);
void dclose(void);

/* logical keys returned by readkey() for nav (above the byte range) */
#define K_LEFT  1001
#define K_RIGHT 1002
#define K_UP    1003
#define K_DOWN  1004
#define K_HOME  1005
#define K_END   1006
#define K_PGUP  1007
#define K_PGDN  1008
#define K_ESC   1009

unsigned char *const SCREEN = (unsigned char *)0x0400;

typedef struct { char *chars; int len; int cap; } erow;
erow row[MAXROWS];              /* row[i].chars == 0 for an empty line */
int numrows = 1;
int cx = 0, cy = 0;             /* cursor: column, row (in the document) */
int rowoff = 0;                 /* first document row shown at screen top */
int coloff = 0;                 /* first document column shown at screen left */
char dirty = 0;                 /* unsaved changes since last load/save */
char curname[16];               /* current filename ("" = none yet) */
char statusmsg[20];             /* transient status text (until next key) */
char full_redraw = 1;           /* 1 = repaint the whole text area next refresh */

static void msg(char *m) { strcpy(statusmsg, m); }

/* grow row r's capacity to at least `need` bytes; 0 = out of memory (old kept) */
static int row_reserve(int r, int need)
{
    char *p; int nc;
    if (need <= row[r].cap) return 1;
    nc = (need + 7) & ~7;                        /* round up to 8 */
    p = realloc(row[r].chars, nc);               /* realloc(0,n) == malloc(n) */
    if (!p) return 0;
    row[r].chars = p; row[r].cap = nc;
    return 1;
}

static void clear_doc(void)
{
    int i;
    for (i = 0; i < numrows; i++) { free(row[i].chars); row[i].chars = 0; row[i].len = 0; row[i].cap = 0; }
    numrows = 0;
}

/* ------------------------------------------------------------------ */
static void appendnum(char *buf, int *pi, int v)
{
    char t[6]; int n = 0;
    do { t[n++] = '0' + v % 10; v /= 10; } while (v);
    while (n) buf[(*pi)++] = t[--n];
}

/* "EDIT name* Lx/y Cz  <msg>" on the reverse-video bottom row */
static void status(void)
{
    char buf[64];
    char *nm = curname[0] ? curname : "[new]";
    int i = 0, c;
    for (c = 0; c < 64; c++) buf[c] = ' ';
    buf[i++] = 'E'; buf[i++] = 'D'; buf[i++] = 'I'; buf[i++] = 'T'; buf[i++] = ' ';
    { char *p = nm; while (*p && i < 18) buf[i++] = *p++; }
    if (dirty) buf[i++] = '*';
    buf[i++] = ' '; buf[i++] = 'L'; appendnum(buf, &i, cy + 1);
    buf[i++] = '/'; appendnum(buf, &i, numrows);
    buf[i++] = ' '; buf[i++] = 'C'; appendnum(buf, &i, cx + 1);
    if (statusmsg[0]) { char *p = statusmsg; buf[i++] = ' '; while (*p && i < 63) buf[i++] = *p++; }
    for (c = 0; c < COLS; c++) SCREEN[TEXTROWS * COLS + c] = buf[c] | 0x80;
}

/* paint one screen text row r (0..TEXTROWS-1), windowed horizontally at coloff */
static void paint_row(int r)
{
    int fr = rowoff + r, c, sc, len = 0;
    unsigned char *p = SCREEN + r * COLS;
    char *src = 0;
    if (fr < numrows) { src = row[fr].chars; len = row[fr].len; }
    for (c = 0; c < COLS; c++) { sc = coloff + c; p[c] = (sc < len) ? (unsigned char)src[sc] : ' '; }
}

static void refresh(void)
{
    int oldr = rowoff, oldc = coloff, r;
    if (cy < rowoff) rowoff = cy;
    if (cy >= rowoff + TEXTROWS) rowoff = cy - TEXTROWS + 1;
    if (cx < coloff) coloff = cx;
    if (cx >= coloff + COLS) coloff = cx - COLS + 1;
    if (rowoff != oldr || coloff != oldc) full_redraw = 1;

    if (full_redraw) {
        for (r = 0; r < TEXTROWS; r++) paint_row(r);
        full_redraw = 0;
    } else {
        paint_row(cy - rowoff);                 /* typing: only the cursor's row */
    }
    SCREEN[(cy - rowoff) * COLS + (cx - coloff)] |= 0x80;   /* block cursor */
    status();
}

/* ------------------------------------------------------------------ */
static void insert_ch(char ch)
{
    erow *e = &row[cy];
    int i;
    if (!row_reserve(cy, e->len + 1)) { msg("No memory"); return; }
    for (i = e->len; i > cx; i--) e->chars[i] = e->chars[i - 1];
    e->chars[cx] = ch;
    e->len++; cx++; dirty = 1;
}

static void newline(void)
{
    int tail = row[cy].len - cx, i;
    char *nc = 0;
    if (numrows >= MAXROWS) { msg("Too many lines"); return; }
    if (tail > 0) {                             /* allocate the new line first */
        nc = malloc((tail + 7) & ~7);
        if (!nc) { msg("No memory"); return; }
        for (i = 0; i < tail; i++) nc[i] = row[cy].chars[cx + i];
    }
    for (i = numrows; i > cy + 1; i--) row[i] = row[i - 1];   /* open a gap */
    row[cy + 1].chars = nc;
    row[cy + 1].len = tail;
    row[cy + 1].cap = nc ? ((tail + 7) & ~7) : 0;
    row[cy].len = cx;                           /* truncate (keep its buffer) */
    numrows++;
    cy++; cx = 0; full_redraw = 1; dirty = 1;
}

static void backspace(void)
{
    int i, plen;
    erow *e = &row[cy];
    if (cx > 0) {                               /* delete the char before the cursor */
        for (i = cx - 1; i < e->len - 1; i++) e->chars[i] = e->chars[i + 1];
        e->len--; cx--; dirty = 1;
        return;
    }
    if (cy == 0) return;
    plen = row[cy - 1].len;                      /* join onto the previous line */
    if (e->len > 0) {
        if (!row_reserve(cy - 1, plen + e->len)) { msg("No memory"); return; }
        for (i = 0; i < e->len; i++) row[cy - 1].chars[plen + i] = e->chars[i];
        row[cy - 1].len = plen + e->len;
    }
    cx = plen;
    free(e->chars);                              /* free the absorbed line */
    for (i = cy; i < numrows - 1; i++) row[i] = row[i + 1];   /* close the gap */
    row[numrows - 1].chars = 0; row[numrows - 1].len = 0; row[numrows - 1].cap = 0;
    numrows--; cy--; full_redraw = 1; dirty = 1;
}

/* read a key, decoding ESC [ X navigation sequences into K_* logical keys. */
static int readkey(void)
{
    int c = INCH();
    if (c != 0x1B) return c;
    c = INCH_NB();
    if (c != '[') return K_ESC;
    c = INCH_NB();
    switch (c) {
        case 'A': return K_UP;
        case 'B': return K_DOWN;
        case 'C': return K_RIGHT;
        case 'D': return K_LEFT;
        case 'H': return K_HOME;
        case 'F': return K_END;
        case '5': INCH_NB(); return K_PGUP;
        case '6': INCH_NB(); return K_PGDN;
    }
    return K_ESC;
}

/* read a line into buf on the status row; 1 = RETURN, 0 = ESC (cancel) */
static int prompt(char *label, char *buf, int max)
{
    int len = 0, k, i, c;
    for (;;) {
        i = 0;
        { char *p = label; while (*p) SCREEN[TEXTROWS * COLS + i++] = (*p++) | 0x80; }
        for (c = 0; c < len; c++) SCREEN[TEXTROWS * COLS + i++] = buf[c] | 0x80;
        while (i < COLS) SCREEN[TEXTROWS * COLS + i++] = ' ' | 0x80;
        k = readkey();
        if (k == K_ESC) return 0;
        if (k == 0x0D || k == 0x0A) { buf[len] = 0; return 1; }
        if (k == 0x08 || k == 0x7F) { if (len > 0) len--; continue; }
        if (k >= 0x20 && k < 0x7F && len < max - 1) buf[len++] = (char)k;
    }
}

static void save(void)
{
    char name[16]; int r, c;
    if (curname[0]) strcpy(name, curname);
    else if (!prompt("Save as: ", name, 16)) { msg("Cancelled"); return; }
    if (dopen_write(name)) { msg("Save failed"); return; }
    for (r = 0; r < numrows; r++) {
        for (c = 0; c < row[r].len; c++) dputb(row[r].chars[c]);
        dputb('\n');                            /* terminate every line */
    }
    dclose();
    strcpy(curname, name);
    dirty = 0;
    msg("Saved");
}

static void load(void)
{
    char name[16]; int c, r;
    if (!prompt("Open: ", name, 16)) { msg("Cancelled"); return; }
    if (dopen_read(name)) { msg("Not found"); return; }
    clear_doc();
    row[0].chars = 0; row[0].len = 0; row[0].cap = 0; numrows = 1;
    for (;;) {
        c = dgetb();
        if (c < 0) break;
        if (c == '\r') continue;                /* tolerate CRLF */
        if (c == '\n') {
            if (numrows >= MAXROWS) break;
            row[numrows].chars = 0; row[numrows].len = 0; row[numrows].cap = 0;
            numrows++;
        } else {
            r = numrows - 1;
            if (row_reserve(r, row[r].len + 1)) row[r].chars[row[r].len++] = (char)c;
        }
    }
    dclose();
    /* a trailing newline made an extra empty row -- drop it (round-trips save) */
    if (numrows > 1 && row[numrows - 1].len == 0) { free(row[numrows - 1].chars); numrows--; }
    cx = cy = rowoff = coloff = 0; dirty = 0; full_redraw = 1;
    strcpy(curname, name);
    msg("Loaded");
}

int main(void)
{
    int k;
    row[0].chars = 0; row[0].len = 0; row[0].cap = 0; numrows = 1;
    /* Park the kernel's blinking cursor off-screen (drawCursor skips x>=40) so
       only the editor's block cursor shows. */
    *(unsigned char *)0x0276 = 40;
    for (;;) {
        refresh();
        k = readkey();
        statusmsg[0] = 0;                        /* clear transient msg on any key */
        switch (k) {
            case K_ESC: QUITDOS(); break;
            case 0x13: save(); break;            /* Ctrl-S */
            case 0x0F: load(); break;            /* Ctrl-O */
            case 0x0D: case 0x0A: newline(); break;
            case 0x08: case 0x7F: backspace(); break;
            case K_LEFT:
                if (cx > 0) cx--;
                else if (cy > 0) { cy--; cx = row[cy].len; full_redraw = 1; }
                break;
            case K_RIGHT:
                if (cx < row[cy].len) cx++;
                else if (cy < numrows - 1) { cy++; cx = 0; full_redraw = 1; }
                break;
            case K_UP:
                if (cy > 0) { cy--; if (cx > row[cy].len) cx = row[cy].len; full_redraw = 1; }
                break;
            case K_DOWN:
                if (cy < numrows - 1) { cy++; if (cx > row[cy].len) cx = row[cy].len; full_redraw = 1; }
                break;
            case K_HOME: cx = 0; break;
            case K_END:  cx = row[cy].len; break;
            case K_PGUP:
                cy -= TEXTROWS; if (cy < 0) cy = 0;
                if (cx > row[cy].len) cx = row[cy].len; full_redraw = 1; break;
            case K_PGDN:
                cy += TEXTROWS; if (cy > numrows - 1) cy = numrows - 1;
                if (cx > row[cy].len) cx = row[cy].len; full_redraw = 1; break;
            default:
                if (k >= 0x20 && k < 0x7F) insert_ch((char)k);
                break;
        }
    }
    return 0;
}
