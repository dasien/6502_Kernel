/*
 *  TERM -- a serial ANSI/VT100 terminal + telnet BBS dialer for MFC-DOS.
 *
 *  Bridges the keyboard and the 80x25 color screen to the 6551 ACIA, which the
 *  host "modem" (Modem.cpp) connects to a TCP/telnet BBS. Keys are forwarded to
 *  the ACIA TX (arrows etc. already arrive as ESC[..] sequences); bytes from the
 *  ACIA RX run through an ANSI interpreter that renders to the screen via the
 *  VIC register port (glue.s vaddr/vputc/vattr + chip-side clear/scroll).
 *
 *  Dialing is in-band Hayes: Ctrl-D prompts for host:port and sends "ATDT ...";
 *  the modem answers CONNECT / NO CARRIER in the byte stream. Ctrl-X hangs up
 *  (+++ ATH), Ctrl-Q returns to the DOS prompt.
 *
 *  ANSI subset (BBS-targeted): CUP (ESC[r;cH/f), CUU/CUD/CUF/CUB, ED (ESC[nJ),
 *  EL (ESC[nK), SGR colors (ESC[...m, 16-color + bright + reverse), cursor
 *  save/restore (ESC[s/u and ESC 7/8), and CR/LF/BS/TAB/BEL. Unknown sequences
 *  are consumed without desyncing the parser. CP437 art is out of scope (the
 *  char plane is 7-bit; bit 7 is the reverse attribute).
 */

/* ---- glue.s ---- */
char INCH(void);                 /* blocking key read (true case) */
int  INCH_NB(void);              /* non-blocking key: 0..255, or -1 */
void QUITDOS(void);              /* return to the DOS ] prompt */
void vaddr(unsigned int cell);   /* point the VIC data port at a cell */
void vputc(unsigned char ch);    /* write a glyph at the cell (auto-increments) */
void vattr(unsigned char a);     /* set the color/attribute latch */
void vcursor(unsigned int cell); /* position the displayed hardware cursor */
void vfill(unsigned char ch);    /* set the fill char for the next chip command */
void vcmd(unsigned char cmd);    /* run a chip-side block op */
void acia_init(void);            /* init the 6551 */
int  acia_get(void);             /* non-blocking RX byte 0..255, or -1 */
void acia_put(unsigned char c);  /* transmit a byte */

#define COLS 80
#define ROWS 25

#define ATTR_DEFAULT 0x02        /* green on black (matches the kernel default) */
#define ATTR_BRIGHT  0x40
#define ATTR_REVERSE 0x80

#define VCMD_CLEAR    0x01
#define VCMD_SCROLLUP 0x02
#define VCMD_FILLROW  0x04

/* local hot-keys (stolen from the remote; chosen to rarely matter to a BBS) */
#define KEY_DIAL   0x04          /* Ctrl-D */
#define KEY_HANGUP 0x18          /* Ctrl-X */
#define KEY_QUIT   0x11          /* Ctrl-Q */

/* cursor + attribute state */
int cx = 0, cy = 0;
int scx = 0, scy = 0;            /* saved cursor (ESC[s / ESC 7) */
unsigned char attr = ATTR_DEFAULT;

/* carrier state, tracked by spotting the modem's result codes in the RX stream.
   Used so we only emit the in-band "+++ATH" hangup while actually connected --
   sending it offline would be parsed by the modem as a bad AT line (ERROR). */
char online = 0;
int  m_connect = 0, m_nocar = 0; /* incremental substring match indices */

/* ANSI parser state */
#define MAXPARAM 8
int param[MAXPARAM];
int nparam;                      /* index of the current param (count-1) */
char priv;                       /* non-zero if a '?' private CSI */
enum pstate { GROUND, GOTESC, GOTCSI };
enum pstate pstate = GROUND;

/* Advance an incremental substring matcher; returns 1 when pat fully matches. */
static int feed_match(const char *pat, int *idx, unsigned char c)
{
    if (c == (unsigned char)pat[*idx]) {
        (*idx)++;
        if (pat[*idx] == 0) { *idx = 0; return 1; }
    } else {
        *idx = (c == (unsigned char)pat[0]) ? 1 : 0;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
static void move_cursor(void) { vcursor((unsigned int)(cy * COLS + cx)); }

static void scroll_up(void) { vfill(' '); vcmd(VCMD_SCROLLUP); }

static void line_feed(void)
{
    cy++;
    if (cy >= ROWS) { scroll_up(); cy = ROWS - 1; }
}

/* write one printable glyph at the cursor and advance (wraps at column 80) */
static void put_glyph(unsigned char ch)
{
    if (cx >= COLS) { cx = 0; line_feed(); }
    vaddr((unsigned int)(cy * COLS + cx));
    vattr(attr);
    vputc(ch & 0x7F);
    cx++;
    if (cx >= COLS) { cx = 0; line_feed(); }
}

/* fill a contiguous run of cells with spaces in the current attribute */
static void fill_cells(unsigned int from, unsigned int to)
{
    unsigned int i;
    vattr(attr);
    vaddr(from);
    for (i = from; i <= to; i++) vputc(' ');
}

static void erase_display(int n)
{
    unsigned int cell = (unsigned int)(cy * COLS + cx);
    if (n == 1) fill_cells(0, cell);                 /* start..cursor */
    else if (n >= 2) { vfill(' '); vcmd(VCMD_CLEAR); } /* whole screen */
    else fill_cells(cell, ROWS * COLS - 1);          /* cursor..end */
}

static void erase_line(int n)
{
    unsigned int row = (unsigned int)(cy * COLS);
    unsigned int cell = row + (unsigned int)cx;
    if (n == 1) fill_cells(row, cell);               /* bol..cursor */
    else if (n >= 2) { vattr(attr); vaddr(row); vfill(' '); vcmd(VCMD_FILLROW); }
    else fill_cells(cell, row + COLS - 1);           /* cursor..eol */
}

static void sgr(void)
{
    int i, p;
    int count = nparam + 1;
    for (i = 0; i < count; i++) {
        p = param[i];
        if (p == 0)              attr = ATTR_DEFAULT;
        else if (p == 1)         attr |= ATTR_BRIGHT;
        else if (p == 2 || p == 22) attr &= ~ATTR_BRIGHT;
        else if (p == 7)         attr |= ATTR_REVERSE;
        else if (p == 27)        attr &= ~ATTR_REVERSE;
        else if (p >= 30 && p <= 37) attr = (attr & ~0x07) | (p - 30);
        else if (p == 39)        attr = (attr & ~0x07) | (ATTR_DEFAULT & 0x07);
        else if (p >= 40 && p <= 47) attr = (attr & ~0x38) | ((p - 40) << 3);
        else if (p == 49)        attr = (attr & ~0x38) | (ATTR_DEFAULT & 0x38);
        /* other SGR codes (blink/underline/256-color) ignored */
    }
    vattr(attr);
}

static void csi_dispatch(unsigned char final)
{
    int n = param[0];
    switch (final) {
    case 'H': case 'f': {                 /* CUP: row;col, 1-based */
        int r = param[0] ? param[0] : 1;
        int c = (nparam >= 1 && param[1]) ? param[1] : 1;
        cy = r - 1; cx = c - 1;
        if (cy < 0) cy = 0; if (cy >= ROWS) cy = ROWS - 1;
        if (cx < 0) cx = 0; if (cx >= COLS) cx = COLS - 1;
        move_cursor();
        break; }
    case 'A': cy -= n ? n : 1; if (cy < 0) cy = 0; move_cursor(); break;
    case 'B': cy += n ? n : 1; if (cy >= ROWS) cy = ROWS - 1; move_cursor(); break;
    case 'C': cx += n ? n : 1; if (cx >= COLS) cx = COLS - 1; move_cursor(); break;
    case 'D': cx -= n ? n : 1; if (cx < 0) cx = 0; move_cursor(); break;
    case 'J': erase_display(n); break;
    case 'K': erase_line(n); break;
    case 'm': sgr(); break;
    case 's': scx = cx; scy = cy; break;
    case 'u': cx = scx; cy = scy; move_cursor(); break;
    default: break;                       /* unknown CSI: ignore, stay synced */
    }
}

static void ansi_byte(unsigned char c)
{
    switch (pstate) {
    case GROUND:
        if (c == 0x1B) { pstate = GOTESC; break; }
        if (c == 0x0D) { cx = 0; move_cursor(); }
        else if (c == 0x0A) { line_feed(); move_cursor(); }
        else if (c == 0x08) { if (cx > 0) cx--; move_cursor(); }
        else if (c == 0x09) { cx = (cx + 8) & ~7; if (cx >= COLS) cx = COLS - 1; move_cursor(); }
        else if (c == 0x07) { /* BEL: ignore */ }
        else if (c >= 0x20) { put_glyph(c); move_cursor(); }
        break;
    case GOTESC:
        if (c == '[') { pstate = GOTCSI; nparam = 0; param[0] = 0; priv = 0; }
        else if (c == '7') { scx = cx; scy = cy; pstate = GROUND; }
        else if (c == '8') { cx = scx; cy = scy; move_cursor(); pstate = GROUND; }
        else pstate = GROUND;             /* other ESC x: ignore */
        break;
    case GOTCSI:
        if (c == '?') { priv = '?'; }
        else if (c >= '0' && c <= '9') { param[nparam] = param[nparam] * 10 + (c - '0'); }
        else if (c == ';') { if (nparam < MAXPARAM - 1) { nparam++; param[nparam] = 0; } }
        else { csi_dispatch(c); pstate = GROUND; }
        break;
    }
}

/* ------------------------------------------------------------------ */
/* print a local (not-from-network) C string straight to the screen */
static void local_print(const char *s) { while (*s) ansi_byte((unsigned char)*s++); }

/* send a C string out the serial line */
static void serial_print(const char *s) { while (*s) acia_put((unsigned char)*s++); }

/* read a line locally (echoed to the screen) into buf; returns on CR */
static void local_line(char *buf, int max)
{
    int len = 0, k;
    for (;;) {
        k = INCH();
        if (k == 0x0D || k == 0x0A) { buf[len] = 0; ansi_byte(0x0D); ansi_byte(0x0A); return; }
        if (k == 0x08 || k == 0x7F) { if (len > 0) { len--; ansi_byte(0x08); ansi_byte(' '); ansi_byte(0x08); } continue; }
        if (k >= 0x20 && k < 0x7F && len < max - 1) { buf[len++] = (char)k; ansi_byte((unsigned char)k); }
    }
}

static void do_dial(void)
{
    char host[64];
    local_print("\r\nDial: ");
    local_line(host, sizeof(host));
    if (host[0]) {
        serial_print("ATDT ");
        serial_print(host);
        acia_put(0x0D);
    }
}

static void do_hangup(void)
{
    if (!online) return;      /* nothing to hang up (offline ATH would be ERROR) */
    serial_print("+++");      /* escape to command mode */
    serial_print("ATH");      /* then hang up */
    acia_put(0x0D);
    online = 0;
}

int main(void)
{
    int b, k;

    acia_init();
    while (acia_get() >= 0) { }   /* flush any stale RX from a prior session */
    vfill(' '); vcmd(VCMD_CLEAR);
    cx = 0; cy = 0; attr = ATTR_DEFAULT; vattr(attr); move_cursor();
    local_print("MFC TERM   ^D dial   ^X hang up   ^Q quit\r\n\n");

    for (;;) {
        b = acia_get();
        if (b >= 0) {
            if (feed_match("CONNECT", &m_connect, (unsigned char)b)) online = 1;
            if (feed_match("NO CARRIER", &m_nocar, (unsigned char)b)) online = 0;
            ansi_byte((unsigned char)b);
            continue;
        }

        k = INCH_NB();
        if (k >= 0) {
            if (k == KEY_QUIT) { do_hangup(); QUITDOS(); }
            else if (k == KEY_DIAL) { do_dial(); }
            else if (k == KEY_HANGUP) { do_hangup(); }
            else acia_put((unsigned char)k);   /* forward to the BBS */
        }
    }
    return 0;
}
