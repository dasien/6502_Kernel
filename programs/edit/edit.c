/*
 *  EDIT -- a minimal full-screen text editor for MFC-DOS (kilo-inspired).
 *
 *  Renders the 80x25 screen through the VIC register port (vaddr/vputc/vgetc in
 *  glue.s; the screen is not in the 64K map), reads keys via the kernel
 *  (GET_KEYSTROKE, case-preserving), and edits a document
 *  held as dynamically-allocated lines (kilo's erow model): a fixed array of
 *  row descriptors, each pointing at a malloc'd string sized to its content, so
 *  capacity is bounded by total text (the heap) rather than a per-line width or
 *  a small row count. Vertical + horizontal scroll, a reverse-video block
 *  cursor and status line, and DOS-FS load/save (Ctrl-O / Ctrl-S).
 *
 *  Ctrl-Q quits to the DOS prompt -- twice in a row when there are unsaved
 *  changes. ESC no longer quits (it only cancels prompts), so a stray ESC can't
 *  lose work.
 */

#include <string.h>
#include <stdlib.h>

#define COLS      80
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
char dclose(void);              /* 0 = ok, 1 = flush/finalize failed */

/* ---- VIC video port (glue.s); the screen is not memory-mapped ---- */
void vaddr(unsigned int cell);  /* point the data port at a cell (0..1999) */
void vputc(unsigned char ch);   /* write a glyph (full 8-bit code); auto-advances */
unsigned char vgetc(void);      /* read the glyph at the current cell; advances */
void vattr(unsigned char a);    /* set the color/attribute latch for next writes */
unsigned char vgetcolor(void);  /* read the attribute at the current cell; advances */
void vputcolor(unsigned char a);/* write the attribute at the current cell; advances */
void vhidecur(void);            /* hide the kernel hardware cursor */

#define ATTR_NORMAL 0x02        /* green on black (the system default) */
#define ATTR_REV    0x82        /* reverse-video bit (0x80) | default */

/* Set reverse-video on the cell at linear index idx by flipping the reverse bit
   in its ATTRIBUTE (the glyph is now a full 8-bit code, so reverse can no longer
   live in the character byte). Read-modify-write on the color plane. */
static void orcell(int idx)
{
    unsigned char a;
    vaddr(idx); a = vgetcolor();
    vaddr(idx); vputcolor(a | 0x80);
}

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

typedef struct { char *chars; int len; int cap; } erow;
erow row[MAXROWS];              /* row[i].chars == 0 for an empty line */
int numrows = 1;
int cx = 0, cy = 0;             /* cursor: column, row (in the document) */
int rowoff = 0;                 /* first document row shown at screen top */
int coloff = 0;                 /* first document column shown at screen left */
char dirty = 0;                 /* unsaved changes since last load/save */
/* Current filename ("" = none yet). Sized to hold any path the DOS accepts
   (DOS_ARGBUF_MAX = 48, i.e. 47 chars + NUL). It used to be 16, which silently
   truncated a path like "SYSTEM/README.TXT" to "SYSTEM/README.T" -- so Ctrl-S
   wrote to a DIFFERENT, new file and the edits to the real one were lost. */
#define NAMEMAX   48
char curname[NAMEMAX];
char statusmsg[32];             /* transient status text (until next key) */
char full_redraw = 1;           /* 1 = repaint the whole text area next refresh */
char quit_armed = 0;            /* a dirty Ctrl-Q was issued; next one quits */

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

/* "MFC EDIT name* Lx/y Cz  <msg>" on the reverse-video bottom row */
static void status(void)
{
    char buf[COLS];
    char *nm = curname[0] ? curname : "[new]";
    int i = 0, c;
    for (c = 0; c < COLS; c++) buf[c] = ' ';
    { char *p = "MFC EDIT "; while (*p) buf[i++] = *p++; }
    { char *p = nm; while (*p && i < 22) buf[i++] = *p++; }
    if (dirty) buf[i++] = '*';
    buf[i++] = ' '; buf[i++] = 'L'; appendnum(buf, &i, cy + 1);
    buf[i++] = '/'; appendnum(buf, &i, numrows);
    buf[i++] = ' '; buf[i++] = 'C'; appendnum(buf, &i, cx + 1);
    if (statusmsg[0]) { char *p = statusmsg; buf[i++] = ' '; while (*p && i < COLS - 1) buf[i++] = *p++; }
    vaddr(TEXTROWS * COLS);
    vattr(ATTR_REV);
    for (c = 0; c < COLS; c++) vputc(buf[c]);
    vattr(ATTR_NORMAL);
}

/* ---- title card ---------------------------------------------------------
 * Three seconds on a timer, and deliberately NOT dismissable by a keypress.
 *
 * Reading the keyboard to cut it short means consuming the key, and there is no way
 * to hand it back -- so anyone who types ahead loses their first keystroke to the
 * splash. That is not hypothetical: it ate the Ctrl-R that starts an XMODEM receive
 * the first time this was built that way, and a user typing ahead into TERM or the
 * editor would lose a character with nothing to show for it.
 *
 * Unsigned subtraction against the start tick, so the 60 Hz counter wrapping every
 * eighteen minutes cannot leave us waiting for one. */
extern unsigned int jiffies(void);
extern void vfill(unsigned char ch);
extern void vcmd(unsigned char cmd);

static void splash_puts(unsigned int cell, const char *s)
{
    vaddr(cell);
    while (*s) vputc((unsigned char)*s++);
}

static void splash(void)
{
    unsigned int t0;

    vattr(ATTR_NORMAL | 0x40);              /* bright */
    vfill(' ');
    vcmd(1);                                /* chip-side clear */
    splash_puts(10 * COLS + 30, "M F C   E D I T O R");
    vattr(ATTR_NORMAL);
    splash_puts(12 * COLS + 28, "FULL-SCREEN TEXT EDITOR");

    t0 = jiffies();
    while ((unsigned int)(jiffies() - t0) < 180) { }
}

/* paint one screen text row r (0..TEXTROWS-1), windowed horizontally at coloff */
static void paint_row(int r)
{
    int fr = rowoff + r, c, sc, len = 0;
    char *src = 0;
    if (fr < numrows) { src = row[fr].chars; len = row[fr].len; }
    vaddr(r * COLS);
    for (c = 0; c < COLS; c++) { sc = coloff + c; vputc((sc < len) ? (unsigned char)src[sc] : ' '); }
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
    orcell((cy - rowoff) * COLS + (cx - coloff));   /* block cursor */
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
        vaddr(TEXTROWS * COLS);
        vattr(ATTR_REV);
        { char *p = label; while (*p) { vputc((unsigned char)*p++); i++; } }
        for (c = 0; c < len; c++) { vputc((unsigned char)buf[c]); i++; }
        while (i < COLS) { vputc(' '); i++; }
        vattr(ATTR_NORMAL);
        k = readkey();
        if (k == K_ESC) return 0;
        if (k == 0x0D || k == 0x0A) { buf[len] = 0; return 1; }
        if (k == 0x08 || k == 0x7F) { if (len > 0) len--; continue; }
        if (k >= 0x20 && k < 0x7F && len < max - 1) buf[len++] = (char)k;
    }
}

/* Write the document to `name`. Every dputb/dclose result is checked: a full disk
   makes FS_PUTB fail partway, and the DOS then finalizes the entry at however many
   bytes made it -- so ignoring the result reported "Saved", cleared the dirty flag,
   and left the user with a silently truncated file (the original contents are gone,
   because opening for write truncates). On failure we keep `dirty` set and do NOT
   adopt `name`, so the document is still recoverable with another Ctrl-S. */
static void save(void)
{
    char name[NAMEMAX]; int r, c;
    if (curname[0]) strcpy(name, curname);
    else if (!prompt("Save as: ", name, NAMEMAX)) { msg("Cancelled"); return; }
    if (dopen_write(name)) { msg("Save failed"); return; }
    for (r = 0; r < numrows; r++) {
        for (c = 0; c < row[r].len; c++) {
            if (dputb(row[r].chars[c])) { dclose(); msg("Save failed - disk full?"); return; }
        }
        if (dputb('\n')) { dclose(); msg("Save failed - disk full?"); return; }
    }
    if (dclose()) { msg("Save failed - disk full?"); return; }
    strcpy(curname, name);
    dirty = 0;
    msg("Saved");
}

/* Read a named file into the document (shared by Ctrl-O and the launch argument). */
static void load_named(const char *name)
{
    int c, r, i, incomplete;
    if (dopen_read((char *)name)) { msg("Not found"); return; }
    clear_doc();
    row[0].chars = 0; row[0].len = 0; row[0].cap = 0; numrows = 1;
    incomplete = 0;
    for (;;) {
        c = dgetb();
        if (c < 0) break;
        if (c == '\r') continue;                /* tolerate CRLF */
        if (c == '\n') {
            if (numrows >= MAXROWS) { incomplete = 1; break; }
            row[numrows].chars = 0; row[numrows].len = 0; row[numrows].cap = 0;
            numrows++;
        } else {
            r = numrows - 1;
            if (row_reserve(r, row[r].len + 1)) row[r].chars[row[r].len++] = (char)c;
            else incomplete = 1;                /* out of heap: this byte is lost */
        }
    }
    dclose();
    /* a trailing newline made an extra empty row -- drop it (round-trips save) */
    if (numrows > 1 && row[numrows - 1].len == 0) { free(row[numrows - 1].chars); numrows--; }
    cx = cy = rowoff = coloff = 0; dirty = 0; full_redraw = 1;

    /* A file we could not hold in full (past MAXROWS, or out of heap mid-line) must
       NOT keep its name: a later Ctrl-S would write the partial document back over
       the original and destroy everything we dropped. Leaving curname empty makes
       Ctrl-S prompt for a name, so overwriting becomes a deliberate act. */
    if (incomplete) { curname[0] = 0; msg("Too big - partial, unnamed"); return; }
    for (i = 0; name[i] && i < NAMEMAX - 1; i++) curname[i] = name[i];
    curname[i] = 0;
    msg("Loaded");
}

static void load(void)
{
    char name[NAMEMAX];
    if (!prompt("Open: ", name, NAMEMAX)) { msg("Cancelled"); return; }
    load_named(name);
}

/* ------------------------------------------------------------------ */
/* Incremental search (Ctrl-F), inspired by kilo. Collect every match position
   in the document so the arrows step through ALL occurrences (including
   several on one line), wrapping around. RETURN keeps the cursor at the
   match; ESC restores the original position. */
#define MAXMATCH 256
static int mr[MAXMATCH], mc[MAXMATCH];   /* match positions (row, col) */
static int nmatch, midx;                 /* match count, current index */
static char search_found;                /* 1 if the cursor is on a match */

static void collect(char *q, int qlen)   /* find all matches into mr/mc */
{
    int r, i, j;
    nmatch = 0; search_found = 0;
    if (qlen == 0) return;
    for (r = 0; r < numrows && nmatch < MAXMATCH; r++)
        for (i = 0; i + qlen <= row[r].len && nmatch < MAXMATCH; i++) {
            for (j = 0; j < qlen; j++) if (row[r].chars[i + j] != q[j]) break;
            if (j == qlen) { mr[nmatch] = r; mc[nmatch] = i; nmatch++; }
        }
}

static void gotomatch(void)              /* move the cursor to match[midx] */
{
    if (nmatch == 0) { search_found = 0; return; }
    if (midx < 0) midx = nmatch - 1;
    if (midx >= nmatch) midx = 0;
    cy = mr[midx]; cx = mc[midx]; search_found = 1;
}

static void draw_search(char *q, int qlen)
{
    int r, c, i = 0, base;
    if (cy < rowoff) rowoff = cy;
    if (cy >= rowoff + TEXTROWS) rowoff = cy - TEXTROWS + 1;
    if (cx < coloff) coloff = cx;
    if (cx >= coloff + COLS) coloff = cx - COLS + 1;
    for (r = 0; r < TEXTROWS; r++) paint_row(r);
    base = (cy - rowoff) * COLS + (cx - coloff);
    if (search_found) for (c = 0; c < qlen && (cx - coloff) + c < COLS; c++) orcell(base + c);
    else orcell(base);                          /* just the cursor when no match */
    vaddr(TEXTROWS * COLS);
    vattr(ATTR_REV);
    { char *p = "Search: "; while (*p) { vputc((unsigned char)*p++); i++; } }
    for (c = 0; c < qlen; c++) { vputc((unsigned char)q[c]); i++; }
    while (i < COLS) { vputc(' '); i++; }
    vattr(ATTR_NORMAL);
}

static void search(void)
{
    char q[40]; int qlen = 0, k;
    int scx = cx, scy = cy, sro = rowoff, sco = coloff;   /* restore on cancel */
    nmatch = 0; midx = 0; search_found = 0; q[0] = 0;
    for (;;) {
        draw_search(q, qlen);
        k = readkey();
        if (k == K_ESC) { cx = scx; cy = scy; rowoff = sro; coloff = sco; full_redraw = 1; msg("Search cancelled"); return; }
        if (k == 0x0D || k == 0x0A) { full_redraw = 1; msg(search_found ? "Found" : "Not found"); return; }
        if (k == 0x08 || k == 0x7F) { if (qlen) qlen--; q[qlen] = 0; collect(q, qlen); midx = 0; gotomatch(); }
        else if (k == K_UP || k == K_LEFT) { midx--; gotomatch(); }       /* previous occurrence */
        else if (k == K_DOWN || k == K_RIGHT) { midx++; gotomatch(); }    /* next occurrence */
        else if (k >= 0x20 && k < 0x7F && qlen < 39) { q[qlen++] = (char)k; q[qlen] = 0; collect(q, qlen); midx = 0; gotomatch(); }
    }
}

int main(void)
{
    int k, i;
    static char argname[48];
    row[0].chars = 0; row[0].len = 0; row[0].cap = 0; numrows = 1;
    vattr(ATTR_NORMAL);          /* normal text attribute for painted rows */
    /* Hide the kernel hardware cursor so only the editor's reverse-video block
       cursor shows; returning to DOS re-shows it on the next PRINT_CHAR. */
    vhidecur();
    splash();
    /* If DOS launched us with a filename argument (e.g. "EDIT SYSTEM/DIAL.LST"),
       open it. DOS leaves the command tail, NUL-terminated, in DOS_ARGBUF ($0382);
       an empty string means no argument, so we start with a blank document. */
    {
        const char *a = (const char *)0x0382;
        for (i = 0; a[i] && i < (int)sizeof(argname) - 1; i++) argname[i] = a[i];
        argname[i] = 0;
    }
    if (argname[0]) load_named(argname);
    for (;;) {
        refresh();
        k = readkey();
        statusmsg[0] = 0;                        /* clear transient msg on any key */
        if (k != 0x11) quit_armed = 0;           /* any non-Ctrl-Q key disarms quit */
        switch (k) {
            case K_ESC: break;                   /* ESC no longer quits (cancels prompts only) */
            case 0x11:                           /* Ctrl-Q: quit, guarded if unsaved */
                if (dirty && !quit_armed) { msg("Unsaved! ^Q to quit"); quit_armed = 1; }
                else QUITDOS();
                break;
            case 0x13: save(); break;            /* Ctrl-S */
            case 0x0F: load(); break;            /* Ctrl-O */
            case 0x06: search(); break;          /* Ctrl-F */
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
