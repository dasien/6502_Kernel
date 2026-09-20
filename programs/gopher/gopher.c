/*
 *  GOPHER -- a Gopher (RFC 1436) client for MFC-DOS, over the emulated modem.
 *
 *  Transport is the same stack as TERM and IRC: bytes go out the 6551 ACIA to
 *  the host modem, which bridges to a TCP socket. Dialing is in-band Hayes, so
 *  we send "ATDT host:port" and wait for the modem's CONNECT result. Gopher is
 *  a request and a response and nothing else: connect, send the selector and
 *  CRLF, read until the far end closes.
 *
 *  A menu line is  <type><display> TAB <selector> TAB <host> TAB <port>  and the
 *  response ends with a line holding just ".". Types handled: 1 menu, 0 text,
 *  7 search, i info (shown, not selectable). Anything else is reported.
 *
 *  Menus are held in RAM, not spooled, because the filesystem has no seek (see
 *  TODO.md) so line offsets would buy nothing. Items go into one packed arena
 *  with a small index; an info line stores only its display text, which is what
 *  keeps a typical menu well inside the budget. A menu past MAXITEM or ARENA is
 *  truncated and says so rather than overrunning.
 *
 *  Known limit: the modem bridge always runs a telnet IAC filter, so a literal
 *  $FF byte is escaped outbound and read as negotiation inbound. Gopher text is
 *  7-bit, so this is invisible -- but it is why type 9 binary is out of scope.
 */

#include <string.h>

#define COLS     80
#define ROWS     25
#define BODY_TOP 1                      /* row 0: where-you-are bar */
#define BODY_BOT (ROWS - 2)             /* last row: status */
#define BODY_H   (BODY_BOT - BODY_TOP + 1)

/* Attribute byte: bit7 reverse, bit6 bright, bits5-3 background, bits2-0
   foreground, over black red green yellow blue magenta cyan white. Keep the
   background bits clear unless a coloured panel is actually wanted. */
#define A_NORM  0x02                    /* green on black, the system default */
#define A_HDR   0x86                    /* reverse + cyan: black on cyan, as IRC */
#define A_SEL   0x82                    /* reverse + green: the highlighted line */
#define A_INFO  0x07                    /* white on black: info text, not a link */
#define A_WARN  0x43                    /* bright yellow on black */

#define ASCII_ESC 0x1B
#define ASCII_CR  0x0D
#define ASCII_LF  0x0A
#define ASCII_BS  0x08
#define ASCII_TAB 0x09

#define K_LEFT 1001
#define K_RIGHT 1002
#define K_UP   1003
#define K_DOWN 1004
#define K_HOME 1005
#define K_END  1006
#define K_PGUP 1007
#define K_PGDN 1008
#define K_ESC  1009

#define MAXITEM  150
#define ARENA    10240
#define DEPTH    16                     /* back-stack depth */
#define MAXBM    9                      /* bookmarks offered, 1..9 */
#define BMBUF    600
#define HOSTMAX  48
#define SELMAX   72

/* ---- kernel / VIC / ACIA glue (glue.s) ---------------------------------- */
extern unsigned char INCH(void);
extern int           INCH_NB(void);
extern void          QUITDOS(void);
extern void          vaddr(unsigned int cell);
extern void          vputc(unsigned char ch);
extern void          vattr(unsigned char attr);
extern void          vcursor(unsigned int cell);
extern void          vfill(unsigned char ch);
extern void          vcmd(unsigned char cmd);
extern void          vscrolltop(unsigned char row);
extern void          vscrollbot(unsigned char row);
extern unsigned int  jiffies(void);
extern char          dopen_read(char *name);  /* 0 = ok, 1 = error */
extern int           dgetb(void);             /* next byte, or -1 at EOF */
extern void          dclose(void);
extern void          acia_init(void);
extern int           acia_get(void);
extern void          acia_put(unsigned char b);

#define VCMD_CLEAR 0x01

/* ---- the current menu ---------------------------------------------------- */
static char          arena[ARENA];
static unsigned int  arena_used;
static unsigned char it_type[MAXITEM];
static unsigned int  it_disp[MAXITEM];  /* offsets into arena */
static unsigned int  it_sel[MAXITEM];
static unsigned int  it_host[MAXITEM];
static unsigned int  it_port[MAXITEM];
static int           n_items;
static char          truncated;

/* where we are, and how we got here */
static char cur_host[HOSTMAX];
static unsigned int cur_port;
static char cur_sel[SELMAX];
static char bk_host[DEPTH][HOSTMAX];
static unsigned int bk_port[DEPTH];
static char bk_sel[DEPTH][SELMAX];
static int  depth;

static int top, sel;                    /* first visible item, selected item */

/* ---- bookmarks (SYSTEM/GOPHER.LST) --------------------------------------
 * One entry per line, "host[:port][/selector]  label", with '#' comments and
 * blank lines ignored -- the same shape as TERM's DIAL.LST and IRC's IRC.LST,
 * extended with the selector Gopher needs to point at anything but a root menu.
 * The address is one whitespace-delimited token and the rest of the line is the
 * label, so a label may contain spaces and a selector may not. */
static char  bmbuf[BMBUF];
static char *bm_host[MAXBM];
static char *bm_sel[MAXBM];
static unsigned int bm_port[MAXBM];
static char *bm_name[MAXBM];
static int   bm_count;

static void load_bookmarks(void)
{
    int n = 0, c;
    char *p;

    bm_count = 0;
    if (dopen_read("SYSTEM/GOPHER.LST")) return;
    while ((c = dgetb()) >= 0 && n < BMBUF - 1) bmbuf[n++] = (char)c;
    dclose();
    bmbuf[n] = 0;

    p = bmbuf;
    while (*p && bm_count < MAXBM) {
        char *line = p, *addr, *q;
        while (*p && *p != '\n' && *p != '\r') p++;
        if (*p) *p++ = 0;
        while (*p == '\n' || *p == '\r') p++;
        while (*line == ' ' || *line == '\t') line++;
        if (*line == 0 || *line == '#') continue;

        addr = line;                              /* the address token */
        while (*line && *line != ' ' && *line != '\t') line++;
        if (*line) { *line++ = 0; while (*line == ' ' || *line == '\t') line++; }
        bm_name[bm_count] = line;                 /* rest of line = label */

        /* Split the address into host, port and selector. The selector starts
           at the first '/' after the host and port, as in a Gopher URL. */
        bm_host[bm_count] = addr;
        bm_port[bm_count] = 70;
        bm_sel[bm_count]  = (char *)"";
        for (q = addr; *q; q++) {
            if (*q == ':') {
                unsigned int pv = 0;
                *q++ = 0;
                while (*q >= '0' && *q <= '9') pv = pv * 10 + (unsigned int)(*q++ - '0');
                if (pv) bm_port[bm_count] = pv;
                if (*q == '/') { *q++ = 0; bm_sel[bm_count] = q; }
                break;
            }
            if (*q == '/') { *q++ = 0; bm_sel[bm_count] = q; break; }
        }
        bm_count++;
    }
}

/* ---- small helpers ------------------------------------------------------- */
/* Bounded copy: the host and selector come off the wire, so a long field in a
   hostile or broken menu must not run off the end of a fixed buffer. */
static void copyn(char *d, const char *s, int max)
{
    int i = 0;
    while (s[i] && i < max - 1) { d[i] = s[i]; i++; }
    d[i] = 0;
}

static void aputs(const char *s) { while (*s) acia_put((unsigned char)*s++); }
static void acrlf(void)          { acia_put('\r'); acia_put('\n'); }
static void hangup(void)         { aputs("+++ATH"); acrlf(); }

/* Read and discard until the line has been quiet for a while. Gopher closes the
   connection itself, so the bridge's NO CARRIER (and the modem's OK from our
   own ATH) arrive AFTER the response terminator and sit unread in the FIFO --
   where the next dial would match them and call the connection failed. */
static void drain(void)
{
    long quiet = 0;
    while (quiet < 20000L) {
        if (acia_get() >= 0) quiet = 0; else quiet++;
    }
}

static void put_at(unsigned int cell, const char *s, int pad)
{
    int i = 0;
    vaddr(cell);
    while (*s && i < COLS) { vputc((unsigned char)*s++); i++; }
    while (i < pad)        { vputc(' '); i++; }
}

/* Bit 7 of the cursor high byte hides it (VREG_CURSOR_HI). The browse view
   shows the selection with reverse video, so the hardware cursor is only ever
   wanted while a prompt is taking input. */
static void hide_cursor(void) { vcursor(0x8000); }

static void status(const char *s)
{
    vattr(A_HDR);
    put_at((unsigned int)(ROWS - 1) * COLS, s, COLS);
    vattr(A_NORM);
}

/* One bar, showing where you are rather than what program this is. */
static void header(void)
{
    char s[COLS + 1];
    int n = 0;
    const char *h = cur_host[0] ? cur_host : "(no host)";

    s[n++] = ' ';
    while (*h && n < COLS - 2) s[n++] = *h++;
    if (cur_sel[0]) {
        const char *q = cur_sel;
        if (n < COLS - 2) s[n++] = ' ';
        while (*q && n < COLS - 1) s[n++] = *q++;
    }
    s[n] = 0;
    vattr(A_HDR);
    put_at(0, s, COLS);
    vattr(A_NORM);
}

/* ---- title card ---------------------------------------------------------
 * Three seconds on a timer, and deliberately NOT dismissable by a keypress, for
 * the reason IRC records: reading the keyboard to cut it short consumes the key
 * with no way to hand it back, so anyone typing ahead loses their first
 * keystroke. Unsigned subtraction against the start tick, so the 60 Hz counter
 * wrapping every eighteen minutes cannot leave us waiting for one. */
static void splash(void)
{
    unsigned int t0;

    vattr(0x47);                            /* bright white */
    vfill(' ');
    vcmd(VCMD_CLEAR);
    put_at(10 * COLS + 30, "M F C   G O P H E R", 0);
    vattr(A_NORM);
    put_at(12 * COLS + 28, "INTERNET GOPHER PROTOCOL", 0);

    t0 = jiffies();
    while ((unsigned int)(jiffies() - t0) < 180) { }
}

static int readkey(void)
{
    int c = INCH();
    if (c != ASCII_ESC) return c;
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

/* Copy s into the arena, NUL-terminated. Returns the offset, or 0xFFFF if full.
   Offset 0 always holds an empty string so 0 is a usable "nothing here". */
static unsigned int intern(const char *s)
{
    unsigned int off = arena_used;
    unsigned int n = (unsigned int)strlen(s);
    if (arena_used + n + 1 > ARENA) return 0;
    memcpy(arena + off, s, n);
    arena[off + n] = 0;
    arena_used += n + 1;
    return off;
}

static int feed_match(const char *s, int *st, unsigned char b)
{
    if (b == (unsigned char)s[*st]) {
        (*st)++;
        if (s[*st] == 0) { *st = 0; return 1; }
    } else {
        *st = (b == (unsigned char)s[0]) ? 1 : 0;
    }
    return 0;
}

/* ---- dial ---------------------------------------------------------------- */
/* 1 connected, 0 failed, -1 cancelled. */
static int dial(const char *host, unsigned int port)
{
    int sc = 0, se = 0, sn = 0, res = 0;
    long t = 12000000L;
    char pbuf[8];
    unsigned int p = port;
    int i = 0, j;

    drain();
    do { pbuf[i++] = (char)('0' + p % 10); p /= 10; } while (p && i < 6);
    aputs("ATDT "); aputs(host); acia_put(':');
    for (j = i - 1; j >= 0; j--) acia_put((unsigned char)pbuf[j]);
    acrlf();

    while (res == 0) {
        int b = acia_get();
        if (b >= 0) {
            if (feed_match("CONNECT",    &sc, (unsigned char)b)) res = 1;
            if (feed_match("ERROR",      &se, (unsigned char)b)) res = 2;
            if (feed_match("NO CARRIER", &sn, (unsigned char)b)) res = 2;
            continue;
        }
        if (INCH_NB() == ASCII_ESC) res = 3;
        if (--t <= 0) res = 2;
    }
    return (res == 1) ? 1 : (res == 3 ? -1 : 0);
}

/* ---- fetch --------------------------------------------------------------- */
/* Reads one response. `as_menu` parses it; otherwise every line is an info
   item, which is how a text file is displayed. Returns 1 on success. */
static int fetch(const char *host, unsigned int port, const char *selector,
                 const char *query, int as_menu)
{
    static char line[256];
    int ln = 0, r;
    long idle = 0;

    n_items = 0; arena_used = 0; truncated = 0;
    arena[arena_used++] = 0;            /* offset 0 = "" */

    status(" Connecting...");
    r = dial(host, port);
    if (r != 1) return 0;

    aputs(selector);
    if (query && *query) { acia_put(ASCII_TAB); aputs(query); }
    acrlf();
    status(" Receiving...  ESC aborts");

    for (;;) {
        int b = acia_get();
        if (b < 0) {
            if (INCH_NB() == ASCII_ESC) break;
            if (++idle > 400000L) break;
            continue;
        }
        idle = 0;
        if (b == ASCII_CR) continue;
        if (b != ASCII_LF) {
            if (ln < (int)sizeof(line) - 1) line[ln++] = (char)b;
            continue;
        }
        line[ln] = 0; ln = 0;
        if (line[0] == '.' && line[1] == 0) break;
        if (!strncmp(line, "NO CARRIER", 10)) break;
        if (n_items >= MAXITEM) { truncated = 1; continue; }

        if (!as_menu) {
            it_type[n_items] = 'i';
            it_disp[n_items] = intern(line);
            it_sel[n_items] = it_host[n_items] = 0;
            it_port[n_items] = 0;
            n_items++;
            continue;
        }

        {   /* split <type><display> TAB sel TAB host TAB port */
            char *f[4];
            int nf = 0, k;
            f[0] = line + 1;
            for (k = 1; line[k] && nf < 3; k++)
                if (line[k] == ASCII_TAB) { line[k] = 0; f[++nf] = line + k + 1; }
            while (nf < 3) f[++nf] = (char *)"";

            it_type[n_items] = (unsigned char)line[0];
            it_disp[n_items] = intern(f[0]);
            if (line[0] == 'i') {
                it_sel[n_items] = it_host[n_items] = 0;
                it_port[n_items] = 0;
            } else {
                it_sel[n_items]  = intern(f[1]);
                it_host[n_items] = intern(f[2]);
                {   unsigned int pv = 0; const char *q = f[3];
                    while (*q >= '0' && *q <= '9') pv = pv * 10 + (unsigned int)(*q++ - '0');
                    it_port[n_items] = pv ? pv : 70; }
            }
            n_items++;
            if (arena_used + 256 > ARENA) truncated = 1;
        }
    }
    hangup();
    drain();
    return 1;
}

/* ---- render -------------------------------------------------------------- */
static int selectable(int i)
{
    unsigned char t = it_type[i];
    return (t == '0' || t == '1' || t == '7');
}

static void draw_item(int row, int i)
{
    unsigned int cell = (unsigned int)(BODY_TOP + row) * COLS;
    const char *d;
    char tag[5];

    if (i >= n_items) { vattr(A_NORM); put_at(cell, "", COLS); return; }
    d = arena + it_disp[i];

    if (!selectable(i)) { vattr(A_INFO); put_at(cell, "  ", 0); put_at(cell + 2, d, COLS - 2); vattr(A_NORM); return; }

    switch (it_type[i]) {
        case '1': strcpy(tag, "/ "); break;
        case '7': strcpy(tag, "? "); break;
        default:  strcpy(tag, "  "); break;
    }
    vattr(i == sel ? A_SEL : A_NORM);
    vaddr(cell);
    vputc((unsigned char)tag[0]); vputc((unsigned char)tag[1]);
    {   int n = 2;
        while (*d && n < COLS) { vputc((unsigned char)*d++); n++; }
        while (n < COLS) { vputc(' '); n++; } }
    vattr(A_NORM);
}

static void draw_all(void)
{
    int r;
    for (r = 0; r < BODY_H; r++) draw_item(r, top + r);
}

static void show_status(void)
{
    char s[COLS + 1];
    int n = 0;
    const char *m = truncated ? " [truncated]" : "";
    strcpy(s, " Enter=open  Bksp=back  Q=quit");
    n = (int)strlen(s);
    while (n < COLS - 14) s[n++] = ' ';
    s[n] = 0;
    strcat(s, m);
    status(s);
}

/* Keep `sel` on screen, moving `top` as little as possible. */
static void scroll_to_sel(void)
{
    if (sel < top) top = sel;
    if (sel >= top + BODY_H) top = sel - BODY_H + 1;
    if (top < 0) top = 0;
}

/* Next selectable item from i in direction delta, or -1 if there is none. */
static int next_selectable(int i, int delta)
{
    for (;;) {
        i += delta;
        if (i < 0 || i >= n_items) return -1;
        if (selectable(i)) return i;
    }
}

/* Move the window without touching the selection. This is the only way to read
   a text file, which is all info lines and so has nothing selectable at all. */
static void scroll_window(int delta)
{
    int nt = top + delta;
    int maxtop = n_items - BODY_H;
    if (maxtop < 0) maxtop = 0;
    if (nt < 0) nt = 0;
    if (nt > maxtop) nt = maxtop;
    if (nt == top) return;
    top = nt;
    draw_all();
}

/* One step. Repaint only the two rows that changed unless the window moved --
   a full body repaint is 23 rows of 80 cells, too much to spend on an arrow. */
static void move_sel(int delta)
{
    int old = sel, oldtop = top, i;

    if (sel < 0) { scroll_window(delta); return; }   /* nothing to select: pan */
    i = next_selectable(sel, delta);
    if (i < 0) return;
    sel = i;
    scroll_to_sel();
    if (top != oldtop) { draw_all(); return; }
    if (old >= top && old < top + BODY_H) draw_item(old - top, old);
    draw_item(sel - top, sel);
}

/* Move the window by `rows` and put the selection on the first selectable item
   now in view. One repaint, not BODY_H of them. */
static void page_sel(int rows)
{
    int nt = top + rows, maxtop = n_items - BODY_H, j, first = -1;

    if (maxtop < 0) maxtop = 0;
    if (nt < 0) nt = 0;
    if (nt > maxtop) nt = maxtop;
    top = nt;
    if (sel >= 0) {
        for (j = top; j < top + BODY_H && j < n_items; j++)
            if (selectable(j)) { first = j; break; }
        if (first >= 0) sel = first;
    }
    draw_all();
}

static void first_selectable(void)
{
    int i;
    sel = -1;
    for (i = 0; i < n_items; i++) if (selectable(i)) { sel = i; break; }
    top = 0;
    if (sel > 0) scroll_to_sel();
}

/* ---- prompts ------------------------------------------------------------- */
static int prompt_at(int row, unsigned char attr, const char *label,
                     char *buf, int max)
{
    int n = 0;
    unsigned int base = (unsigned int)row * COLS;
    unsigned int off = (unsigned int)strlen(label);
    vattr(attr);
    put_at(base, label, COLS);
    vcursor(base + off);
    for (;;) {
        int k = readkey();
        if (k == ASCII_CR)  { buf[n] = 0; vattr(A_NORM); hide_cursor(); return n; }
        if (k == K_ESC)     { buf[0] = 0; vattr(A_NORM); hide_cursor(); return -1; }
        if (k == ASCII_BS) {
            if (n > 0) { n--; vaddr(base + off + (unsigned int)n); vputc(' '); vcursor(base + off + (unsigned int)n); }
            continue;
        }
        if (k < 0x20 || k > 0x7E || n >= max - 1) continue;
        buf[n] = (char)k;
        vaddr(base + off + (unsigned int)n);
        vputc((unsigned char)k);
        n++;
        vcursor(base + off + (unsigned int)n);
    }
}

/* ---- navigation ---------------------------------------------------------- */
static void enter_page(const char *host, unsigned int port, const char *sel_s,
                       const char *query, int as_menu)
{
    int ok;
    hide_cursor();
    vfill(' '); vcmd(VCMD_CLEAR);
    copyn(cur_host, host, HOSTMAX);
    cur_port = port;
    copyn(cur_sel, sel_s, SELMAX);
    header();
    ok = fetch(host, port, sel_s, query, as_menu);
    if (!ok) n_items = 0;
    first_selectable();
    draw_all();                         /* paints the body, so it must come first */
    if (!ok) {
        vattr(A_WARN);
        put_at((unsigned int)BODY_TOP * COLS,
               "  Connection failed. Bksp goes back, Q quits.", COLS);
        vattr(A_NORM);
    }
    show_status();
}

static void push(void)
{
    if (depth >= DEPTH) {               /* drop the oldest */
        int i;
        for (i = 1; i < DEPTH; i++) {
            copyn(bk_host[i - 1], bk_host[i], HOSTMAX);
            copyn(bk_sel[i - 1], bk_sel[i], SELMAX);
            bk_port[i - 1] = bk_port[i];
        }
        depth = DEPTH - 1;
    }
    copyn(bk_host[depth], cur_host, HOSTMAX);
    copyn(bk_sel[depth], cur_sel, SELMAX);
    bk_port[depth] = cur_port;
    depth++;
}

int main(void)
{
    static char host[HOSTMAX];
    static char sels[SELMAX];
    static char query[64];
    static char start_sel[SELMAX];
    unsigned int start_port;

    acia_init();
    vattr(A_NORM);
    vfill(' '); vcmd(VCMD_CLEAR);
    vscrolltop(0); vscrollbot(ROWS - 1);

    splash();

    /* Connection screen, in the same shape as IRC's: title line, blank, then
       the options indented two spaces. */
    vattr(A_NORM);
    vfill(' '); vcmd(VCMD_CLEAR);
    put_at(0 * COLS, "MFC GOPHER v1.0   (ESC quits)", COLS);
    put_at(2 * COLS, "  Up/Down     move the selection", 0);
    put_at(3 * COLS, "  Enter       open the selected item  ( / menu   ? search )", 0);
    put_at(4 * COLS, "  Bksp        back to the previous page", 0);
    put_at(5 * COLS, "  Home        top of the page", 0);
    put_at(6 * COLS, "  Q           quit to DOS", 0);

    cur_host[0] = 0;
    start_port = 70;
    start_sel[0] = 0;
    host[0] = 0;

    load_bookmarks();
    if (bm_count) {
        int row = 8, i, k;
        put_at((unsigned int)row++ * COLS, "Gopher holes:", COLS);
        for (i = 0; i < bm_count; i++) {
            char ln[COLS + 1];
            const char *nm = bm_name[i][0] ? bm_name[i] : bm_host[i];
            int j = 0, m = 0;
            ln[j++] = ' '; ln[j++] = ' ';
            ln[j++] = (char)('1' + i); ln[j++] = ')'; ln[j++] = ' ';
            while (nm[m] && j < COLS) ln[j++] = nm[m++];
            ln[j] = 0;
            put_at((unsigned int)row++ * COLS, ln, COLS);
        }
        put_at((unsigned int)row++ * COLS, "  0) Enter a host", COLS);
        put_at((unsigned int)row * COLS, "Pick: ", 0);
        vcursor((unsigned int)row * COLS + 6);
        k = INCH();
        row++;
        if (k == ASCII_ESC) { QUITDOS(); return 0; }
        if (k >= '1' && k < '1' + bm_count) {
            copyn(host, bm_host[k - '1'], HOSTMAX);
            copyn(start_sel, bm_sel[k - '1'], SELMAX);
            start_port = bm_port[k - '1'];
        }
        if (!host[0] && prompt_at(row, A_NORM, "Host:     ",
                                  host, (int)sizeof(host)) < 0) { QUITDOS(); return 0; }
    } else {
        if (prompt_at(8, A_NORM, "Host:     ",
                      host, (int)sizeof(host)) < 0) { QUITDOS(); return 0; }
    }
    if (!host[0]) strcpy(host, "gopher.floodgap.com");

    depth = 0;
    enter_page(host, start_port, start_sel, 0, 1);

    for (;;) {
        int k = readkey();

        if (k == 'q' || k == 'Q') break;
        if (k == K_UP)   { move_sel(-1); continue; }
        if (k == K_DOWN) { move_sel(+1); continue; }
        if (k == K_PGUP) { page_sel(-BODY_H); continue; }
        if (k == K_PGDN) { page_sel(+BODY_H); continue; }
        if (k == K_HOME) { first_selectable(); draw_all(); continue; }
        if (k == K_END)  { page_sel(n_items); continue; }   /* clamps to the last page */

        if (k == ASCII_BS || k == K_LEFT) {
            if (depth == 0) { status(" Already at the first page.  Q=quit"); continue; }
            depth--;
            copyn(sels, bk_sel[depth], SELMAX);
            enter_page(bk_host[depth], bk_port[depth], sels, 0, 1);
            continue;
        }

        if (k == ASCII_CR || k == K_RIGHT) {
            unsigned char t;
            if (sel < 0) continue;
            t = it_type[sel];
            copyn(sels, arena + it_sel[sel], SELMAX);
            copyn(host, arena + it_host[sel], HOSTMAX);
            if (!host[0]) { status(" That item has no host.  Q=quit"); continue; }

            if (t == '7') {
                if (prompt_at(ROWS - 1, A_HDR, " Search: ", query, (int)sizeof(query)) < 0) { show_status(); continue; }
                push();
                enter_page(host, it_port[sel], sels, query, 1);
                continue;
            }
            if (t == '1' || t == '0') {
                push();
                enter_page(host, it_port[sel], sels, 0, (t == '1'));
                continue;
            }
            status(" Unsupported item type.  Q=quit");
            continue;
        }
    }

    vfill(' '); vcmd(VCMD_CLEAR);
    QUITDOS();
    return 0;
}
