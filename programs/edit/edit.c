/*
 *  EDIT -- a minimal full-screen text editor for MFC-DOS (kilo-inspired spike).
 *
 *  This is the vertical-slice spike for the kilo port: it proves the editor
 *  architecture on our platform end to end --
 *    - render directly to the 40x25 screen RAM at $0400 (no ANSI/terminal),
 *    - read keys via the kernel (GET_KEYSTROKE, now case-preserving),
 *    - basic editing: type, RETURN (split line), backspace (join),
 *    - vertical scroll as the cursor leaves the window,
 *    - a reverse-video cursor cell + a reverse-video status line.
 *
 *  Deliberately trimmed for the spike: a fixed row table (no malloc yet),
 *  lines clipped to the 40-column width (no horizontal scroll), no file
 *  load/save, no arrow keys (the emulator key scheme + DOS FS load/save land
 *  in the full port). ESC quits to the DOS prompt.
 */

#define COLS      40
#define TEXTROWS  24            /* rows 0..23 are text; row 24 is the status line */
#define MAXROWS   150
#define MAXLEN    39            /* max chars per line (keeps the cursor on screen) */

/* ---- kernel hooks (glue.s) ---- */
char INCH(void);                /* blocking: returns the next key, true case */
int  INCH_NB(void);             /* non-blocking: next key 0..255, or -1 if none */
void QUITDOS(void);             /* return to the DOS ] prompt */

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

char rows[MAXROWS][MAXLEN + 1];
unsigned char rowlen[MAXROWS];
int numrows = 1;
int cx = 0, cy = 0;             /* cursor: column, row (in the document) */
int rowoff = 0;                 /* first document row shown at screen top */

static void putstat(int col, char c) { SCREEN[TEXTROWS * COLS + col] = c | 0x80; }

static void status(void)
{
    /* "EDIT  L<cy+1>/<numrows> C<cx+1>" on the reverse-video bottom row */
    char buf[COLS];
    int i, n, v;
    for (i = 0; i < COLS; i++) buf[i] = ' ';
    buf[0] = 'E'; buf[1] = 'D'; buf[2] = 'I'; buf[3] = 'T';
    i = 6; buf[i++] = 'L';
    v = cy + 1; n = 0; { char t[6]; do { t[n++] = '0' + v % 10; v /= 10; } while (v);
                         while (n) buf[i++] = t[--n]; }
    buf[i++] = '/';
    v = numrows; n = 0; { char t[6]; do { t[n++] = '0' + v % 10; v /= 10; } while (v);
                          while (n) buf[i++] = t[--n]; }
    buf[i++] = ' '; buf[i++] = 'C';
    v = cx + 1; n = 0; { char t[6]; do { t[n++] = '0' + v % 10; v /= 10; } while (v);
                         while (n) buf[i++] = t[--n]; }
    for (i = 0; i < COLS; i++) putstat(i, buf[i]);
}

char full_redraw = 1;           /* 1 = repaint the whole text area next refresh */

/* paint one screen text row r (0..TEXTROWS-1) from its document row */
static void paint_row(int r)
{
    int fr = rowoff + r, c, len = 0;
    unsigned char *p = SCREEN + r * COLS;       /* row base, computed once */
    char *src = rows[0];
    if (fr < numrows) { src = rows[fr]; len = rowlen[fr]; }
    for (c = 0; c < COLS; c++) p[c] = (c < len) ? (unsigned char)src[c] : ' ';
}

static void refresh(void)
{
    int old = rowoff, r;
    /* keep the cursor row inside the window */
    if (cy < rowoff) rowoff = cy;
    if (cy >= rowoff + TEXTROWS) rowoff = cy - TEXTROWS + 1;
    if (rowoff != old) full_redraw = 1;         /* scrolled -> repaint all rows */

    if (full_redraw) {
        for (r = 0; r < TEXTROWS; r++) paint_row(r);
        full_redraw = 0;
    } else {
        paint_row(cy - rowoff);                 /* typing: only the cursor's row */
    }
    SCREEN[(cy - rowoff) * COLS + cx] |= 0x80;   /* reverse-video block cursor */
    status();
}

static void insert_ch(char ch)
{
    int i;
    if (rowlen[cy] >= MAXLEN) return;           /* line full (spike: no h-scroll) */
    for (i = rowlen[cy]; i > cx; i--) rows[cy][i] = rows[cy][i - 1];
    rows[cy][cx] = ch;
    rowlen[cy]++;
    cx++;
}

static void newline(void)
{
    int i;
    if (numrows >= MAXROWS) return;
    for (i = numrows; i > cy + 1; i--) {        /* open a gap below the cursor row */
        int j;
        for (j = 0; j <= MAXLEN; j++) rows[i][j] = rows[i - 1][j];
        rowlen[i] = rowlen[i - 1];
    }
    /* new row = tail of the current row from cx */
    for (i = cx; i < rowlen[cy]; i++) rows[cy + 1][i - cx] = rows[cy][i];
    rowlen[cy + 1] = rowlen[cy] - cx;
    rowlen[cy] = cx;
    numrows++;
    cy++; cx = 0;
    full_redraw = 1;            /* rows shifted: repaint everything */
}

static void backspace(void)
{
    int i;
    if (cx > 0) {                               /* delete the char before the cursor */
        for (i = cx - 1; i < rowlen[cy] - 1; i++) rows[cy][i] = rows[cy][i + 1];
        rowlen[cy]--; cx--;
        return;
    }
    if (cy == 0) return;                         /* top-left: nothing to do */
    /* join this row onto the end of the previous one (if it fits) */
    cx = rowlen[cy - 1];
    for (i = 0; i < rowlen[cy] && cx + i < MAXLEN; i++)
        rows[cy - 1][cx + i] = rows[cy][i];
    rowlen[cy - 1] = cx + rowlen[cy];
    if (rowlen[cy - 1] > MAXLEN) rowlen[cy - 1] = MAXLEN;
    for (i = cy; i < numrows - 1; i++) {        /* close the gap */
        int j;
        for (j = 0; j <= MAXLEN; j++) rows[i][j] = rows[i + 1][j];
        rowlen[i] = rowlen[i + 1];
    }
    numrows--;
    cy--;
    full_redraw = 1;            /* rows shifted: repaint everything */
}

/* read a key, decoding ESC [ X navigation sequences into K_* logical keys.
   A bare ESC (nothing queued after it) is K_ESC. */
static int readkey(void)
{
    int c = INCH();
    if (c != 0x1B) return c;
    c = INCH_NB();
    if (c != '[') return K_ESC;             /* bare ESC (or unknown) */
    c = INCH_NB();
    switch (c) {
        case 'A': return K_UP;
        case 'B': return K_DOWN;
        case 'C': return K_RIGHT;
        case 'D': return K_LEFT;
        case 'H': return K_HOME;
        case 'F': return K_END;
        case '5': INCH_NB(); return K_PGUP; /* consume the trailing '~' */
        case '6': INCH_NB(); return K_PGDN;
    }
    return K_ESC;
}

int main(void)
{
    int k;
    rowlen[0] = 0;
    /* Park the kernel's blinking underbar cursor off-screen (drawCursor skips
       cursor_x >= 40) so only the editor's block cursor shows. */
    *(unsigned char *)0x0276 = 40;
    for (;;) {
        refresh();
        k = readkey();
        switch (k) {
            case K_ESC: QUITDOS(); break;
            case 0x0D: case 0x0A: newline(); break;
            case 0x08: case 0x7F: backspace(); break;
            case K_LEFT:
                if (cx > 0) cx--;
                else if (cy > 0) { cy--; cx = rowlen[cy]; full_redraw = 1; }
                break;
            case K_RIGHT:
                if (cx < rowlen[cy]) cx++;
                else if (cy < numrows - 1) { cy++; cx = 0; full_redraw = 1; }
                break;
            case K_UP:
                if (cy > 0) { cy--; if (cx > rowlen[cy]) cx = rowlen[cy]; full_redraw = 1; }
                break;
            case K_DOWN:
                if (cy < numrows - 1) { cy++; if (cx > rowlen[cy]) cx = rowlen[cy]; full_redraw = 1; }
                break;
            case K_HOME: cx = 0; break;
            case K_END:  cx = rowlen[cy]; break;
            case K_PGUP:
                cy -= TEXTROWS; if (cy < 0) cy = 0;
                if (cx > rowlen[cy]) cx = rowlen[cy]; full_redraw = 1; break;
            case K_PGDN:
                cy += TEXTROWS; if (cy > numrows - 1) cy = numrows - 1;
                if (cx > rowlen[cy]) cx = rowlen[cy]; full_redraw = 1; break;
            default:
                if (k >= 0x20 && k < 0x7F) insert_ch((char)k);
                break;
        }
    }
    return 0;
}
