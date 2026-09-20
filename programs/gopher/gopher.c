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
#define BODY_TOP 2                      /* rows 0-1: header */
#define BODY_BOT (ROWS - 2)             /* last row: status */
#define BODY_H   (BODY_BOT - BODY_TOP + 1)

#define A_NORM  0x02                    /* green on black */
#define A_HDR   0x4A                    /* reverse + bright green */
#define A_SEL   0x8A                    /* reverse: the highlighted line */
#define A_INFO  0x07                    /* white: info text, not a link */
#define A_WARN  0x43                    /* bright yellow */

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

static void status(const char *s)
{
    vattr(A_HDR);
    put_at((unsigned int)(ROWS - 1) * COLS, s, COLS);
    vattr(A_NORM);
}

static void header(void)
{
    vattr(A_HDR);
    put_at(0, " MFC GOPHER 1.0", COLS);
    put_at(COLS, cur_host[0] ? cur_host : " (no host)", COLS);
    vattr(A_NORM);
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

static void move_sel(int delta)
{
    int i = sel;
    for (;;) {
        i += delta;
        if (i < 0 || i >= n_items) return;      /* no further selectable item */
        if (selectable(i)) { sel = i; scroll_to_sel(); draw_all(); return; }
    }
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
static int prompt(const char *label, char *buf, int max)
{
    int n = 0;
    unsigned int base = (unsigned int)(ROWS - 1) * COLS;
    unsigned int off = (unsigned int)strlen(label);
    vattr(A_HDR);
    put_at(base, label, COLS);
    vcursor(base + off);
    for (;;) {
        int k = readkey();
        if (k == ASCII_CR)  { buf[n] = 0; vattr(A_NORM); vcursor(0x8000); return n; }
        if (k == K_ESC)     { buf[0] = 0; vattr(A_NORM); vcursor(0x8000); return -1; }
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

    acia_init();
    vattr(A_NORM);
    vfill(' '); vcmd(VCMD_CLEAR);
    vscrolltop(0); vscrollbot(ROWS - 1);

    cur_host[0] = 0;
    header();
    if (prompt(" Host [gopher.floodgap.com]: ", host, (int)sizeof(host)) < 0) { QUITDOS(); return 0; }
    if (!host[0]) strcpy(host, "gopher.floodgap.com");

    depth = 0;
    enter_page(host, 70, "", 0, 1);

    for (;;) {
        int k = readkey();

        if (k == 'q' || k == 'Q') break;
        if (k == K_UP)   { move_sel(-1); continue; }
        if (k == K_DOWN) { move_sel(+1); continue; }
        if (k == K_PGUP) { int i; for (i = 0; i < BODY_H; i++) move_sel(-1); continue; }
        if (k == K_PGDN) { int i; for (i = 0; i < BODY_H; i++) move_sel(+1); continue; }
        if (k == K_HOME) { first_selectable(); draw_all(); continue; }

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
                if (prompt(" Search: ", query, (int)sizeof(query)) < 0) { show_status(); continue; }
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
