/*
 *  GOPHER -- a Gopher (RFC 1436) client for MFC-DOS, over the emulated modem.
 *
 *  SPIKE (phase 1 of 4). This proves the transport only: dial host:70, send a
 *  selector, and dump the raw response to the screen. No menu parsing, no
 *  navigation, no spooling. Phases 2-4 are recorded in ../../TODO.md.
 *
 *  Transport is the same stack as TERM and IRC: bytes go out the 6551 ACIA to
 *  the host modem, which bridges to a TCP socket. Dialing is in-band Hayes, so
 *  we send "ATDT host:port" and wait for the modem's CONNECT result.
 *
 *  Gopher is a request and a response and nothing else. Connect, send the
 *  selector followed by CRLF, then read until the far end closes. A menu is
 *  tab-separated lines ending in a lone ".", but that is phase 2's problem.
 *
 *  Known limit: the modem bridge always runs a telnet IAC filter, so a literal
 *  $FF byte is escaped outbound and read as negotiation inbound. Gopher text is
 *  7-bit, so this is invisible here exactly as it is for IRC -- but it is why
 *  type 9 binary retrieval is out of scope.
 */

#include <string.h>

#define COLS     80
#define ROWS     25
#define BODY_TOP 2                      /* rows 0-1 are the header */
#define BODY_BOT (ROWS - 2)             /* last row is the status line */

#define A_NORM   0x02                   /* green on black, the default */
#define A_HDR    0x4A                   /* reverse + bright green */
#define A_WARN   0x43                   /* bright yellow */

#define ASCII_ESC 0x1B
#define ASCII_CR  0x0D
#define ASCII_LF  0x0A
#define ASCII_BS  0x08

/* ---- kernel / VIC / ACIA glue (glue.s) ---------------------------------- */
extern unsigned char INCH(void);        /* blocking key read */
extern int           INCH_NB(void);     /* -1 when no key waiting */
extern void          QUITDOS(void);
extern void          vaddr(unsigned int cell);
extern void          vputc(unsigned char ch);
extern void          vattr(unsigned char attr);
extern void          vcursor(unsigned int cell);
extern void          vfill(unsigned char ch);        /* set the command parameter */
extern void          vcmd(unsigned char cmd);        /* run it -- param comes first */
extern void          vscrolltop(unsigned char row);
extern void          vscrollbot(unsigned char row);
extern void          acia_init(void);
extern int           acia_get(void);    /* -1 when nothing waiting */
extern void          acia_put(unsigned char b);

#define VCMD_CLEAR    0x01
#define VCMD_SCROLLUP 0x02

/* ---- small helpers ------------------------------------------------------ */
static void aputs(const char *s) { while (*s) acia_put((unsigned char)*s++); }
static void acrlf(void)          { acia_put('\r'); acia_put('\n'); }
static void hangup(void)         { aputs("+++ATH"); acrlf(); }

/* Write s at a cell, then pad with spaces out to `pad` columns. */
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

/* ---- the body region: a plain scrolling dump ---------------------------- */
static int cur_row, cur_col;

static void body_home(void)
{
    cur_row = BODY_TOP;
    cur_col = 0;
    vaddr((unsigned int)cur_row * COLS);
}

static void body_newline(void)
{
    cur_col = 0;
    if (cur_row < BODY_BOT) {
        cur_row++;
    } else {
        /* Scroll just the body: the region was set with vscrolltop/bot below. */
        vfill(' ');
        vcmd(VCMD_SCROLLUP);
    }
    vaddr((unsigned int)cur_row * COLS);
}

static void body_puts(const char *s)
{
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (c == '\t')             c = ' ';   /* phase 2 splits on tabs */
        if (c < 0x20 || c > 0x7E)  c = '.';   /* keep the dump readable */
        if (cur_col >= COLS) body_newline();
        vputc(c);
        cur_col++;
    }
    body_newline();
}

/* ---- a one-line prompt on a given row ----------------------------------- */
static int prompt(int row, const char *label, char *buf, int max)
{
    int n = 0;
    unsigned int base = (unsigned int)row * COLS;
    put_at(base, label, COLS);
    vcursor(base + (unsigned int)strlen(label));
    for (;;) {
        unsigned char k = INCH();
        if (k == ASCII_CR)  { buf[n] = 0; return n; }
        if (k == ASCII_ESC) { buf[0] = 0; return -1; }
        if (k == ASCII_BS) {
            if (n > 0) {
                n--;
                vaddr(base + (unsigned int)strlen(label) + (unsigned int)n);
                vputc(' ');
                vcursor(base + (unsigned int)strlen(label) + (unsigned int)n);
            }
            continue;
        }
        if (k < 0x20 || k > 0x7E || n >= max - 1) continue;
        buf[n] = (char)k;
        vaddr(base + (unsigned int)strlen(label) + (unsigned int)n);
        vputc(k);
        n++;
        vcursor(base + (unsigned int)strlen(label) + (unsigned int)n);
    }
}

/* Match s incrementally against a stream of bytes; *st carries the state. */
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
/* Returns 1 connected, 0 failed, -1 cancelled. */
static int dial(const char *hostport)
{
    int sc = 0, se = 0, sn = 0, res = 0;
    long t = 12000000L;

    aputs("ATDT "); aputs(hostport); acrlf();
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

/* ---- main ---------------------------------------------------------------- */
int main(void)
{
    static char host[64];
    static char sel[128];
    static char line[256];
    int r, idle, ln;

    acia_init();
    vattr(A_NORM);
    vfill(' ');
    vcmd(VCMD_CLEAR);

    vattr(A_HDR);
    put_at(0, " MFC GOPHER (spike)   ESC quits", COLS);
    vattr(A_NORM);

    if (prompt(BODY_TOP,     "Host:port  ", host, (int)sizeof(host)) < 0) { QUITDOS(); return 0; }
    if (!host[0]) strcpy(host, "gopher.floodgap.com:70");
    if (prompt(BODY_TOP + 1, "Selector   ", sel,  (int)sizeof(sel))  < 0) { QUITDOS(); return 0; }

    status(" Connecting...");
    r = dial(host);
    if (r != 1) {
        vattr(A_WARN);
        put_at((unsigned int)(BODY_TOP + 3) * COLS,
               (r == 0) ? "Connection failed. Any key to quit."
                        : "Cancelled. Any key to quit.", COLS);
        vattr(A_NORM);
        INCH();
        QUITDOS();
        return 0;
    }

    /* Clear, then confine the chip's scroll to the body. A clear resets the
     * region to the whole screen, so the order matters. */
    vfill(' ');
    vcmd(VCMD_CLEAR);
    vattr(A_HDR);
    put_at(0, " MFC GOPHER (spike)   ESC quits", COLS);
    vattr(A_NORM);
    vscrolltop(BODY_TOP);
    vscrollbot(BODY_BOT);               /* keep the status line out of it */
    body_home();

    /* Send the selector, then dump whatever comes back. */
    aputs(sel); acrlf();
    status(" Receiving...  ESC aborts");

    /* Read a line at a time. Gopher ends a response with a line holding just
     * ".", then closes, at which point the bridge emits NO CARRIER -- so take
     * either as the end and keep both out of the body. The idle counter is only
     * a backstop for a server that does neither. */
    idle = 0;
    ln = 0;
    for (;;) {
        int b = acia_get();
        if (b < 0) {
            if (INCH_NB() == ASCII_ESC) break;
            if (++idle > 400000) break;
            continue;
        }
        idle = 0;
        if (b == ASCII_CR) continue;                  /* CRLF arrives as a pair */
        if (b != ASCII_LF) {
            if (ln < (int)sizeof(line) - 1) line[ln++] = (char)b;
            continue;
        }
        line[ln] = 0;
        ln = 0;
        if (line[0] == '.' && line[1] == 0) break;    /* Gopher terminator */
        if (!strncmp(line, "NO CARRIER", 10)) break;  /* far end closed */
        body_puts(line);
    }

    hangup();
    status(" End of response. Any key to quit.");
    INCH();
    vscrolltop(0);                      /* hand the full screen back */
    vscrollbot(ROWS - 1);
    QUITDOS();
    return 0;
}
