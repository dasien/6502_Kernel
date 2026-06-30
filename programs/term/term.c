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

/* ---- DOS FAT16 file I/O (glue.s) for XMODEM transfers ---- */
char dopen_read(char *name);     /* 0 = ok, 1 = error */
char dopen_write(char *name);    /* 0 = ok, 1 = error */
int  dgetb(void);                /* next byte 0..255, or -1 at EOF */
char dputb(char c);              /* 0 = ok, 1 = error */
void dclose(void);

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
#define KEY_RECV   0x12          /* Ctrl-R: XMODEM receive */
#define KEY_SEND   0x13          /* Ctrl-S: XMODEM send */
#define KEY_HANGUP 0x18          /* Ctrl-X */
#define KEY_QUIT   0x11          /* Ctrl-Q */

/* XMODEM protocol bytes */
#define X_SOH 0x01
#define X_EOT 0x04
#define X_ACK 0x06
#define X_NAK 0x15
#define X_CAN 0x18
#define X_PAD 0x1A               /* padding for a short final block */

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
    vputc(ch);                  /* full 8-bit CP437 code point (BBS art) */
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

/* Read a line locally (echoed to the screen) into buf. Returns 1 on RETURN, or
   0 if ESC cancelled the entry (buf is emptied). */
static int local_line(char *buf, int max)
{
    int len = 0, k;
    for (;;) {
        k = INCH();
        if (k == 0x1B) { buf[0] = 0; ansi_byte(0x0D); ansi_byte(0x0A); return 0; } /* ESC cancels */
        if (k == 0x0D || k == 0x0A) { buf[len] = 0; ansi_byte(0x0D); ansi_byte(0x0A); return 1; }
        if (k == 0x08 || k == 0x7F) { if (len > 0) { len--; ansi_byte(0x08); ansi_byte(' '); ansi_byte(0x08); } continue; }
        if (k >= 0x20 && k < 0x7F && len < max - 1) { buf[len++] = (char)k; ansi_byte((unsigned char)k); }
    }
}

/* ===================== saved BBS dial-list (DIAL.LST) ====================== */
/* A plain-text list in the FAT16 root, one entry per line: "host:port  name"
   (first whitespace-delimited token is the address, the rest is the display
   name). Blank lines and lines starting with '#' are ignored. Editable in EDIT.
   Up to 9 entries are offered in the ^D menu (single-key selection). */
#define MAXBBS   9
#define DIALBUF  600
static char  dialbuf[DIALBUF];
static char *bbs_addr[MAXBBS];   /* host:port, null-terminated */
static char *bbs_name[MAXBBS];   /* display name (may be empty) */
static int   bbs_count;

static void load_dial_list(void)
{
    int n = 0, c;
    char *p;
    bbs_count = 0;
    if (dopen_read("DIAL.LST")) return;                 /* no saved list */
    while ((c = dgetb()) >= 0 && n < DIALBUF - 1) dialbuf[n++] = (char)c;
    dclose();
    dialbuf[n] = 0;
    p = dialbuf;
    while (*p && bbs_count < MAXBBS) {
        char *line = p;
        while (*p && *p != '\n' && *p != '\r') p++;     /* find end of line */
        if (*p) *p++ = 0;                               /* terminate it */
        while (*p == '\n' || *p == '\r') p++;           /* skip the CR/LF pair */
        while (*line == ' ' || *line == '\t') line++;   /* trim leading space */
        if (*line == 0 || *line == '#') continue;       /* blank / comment */
        bbs_addr[bbs_count] = line;                     /* address = first token */
        while (*line && *line != ' ' && *line != '\t') line++;
        if (*line) { *line++ = 0; while (*line == ' ' || *line == '\t') line++; }
        bbs_name[bbs_count] = line;                     /* name = the rest */
        bbs_count++;
    }
}

static void dial_addr(const char *host)
{
    serial_print("ATDT ");
    serial_print(host);
    acia_put(0x0D);
}

static void do_dial(void)
{
    char host[64];
    int i, k;
    load_dial_list();
    if (bbs_count) {
        local_print("\r\nSaved BBSes:\r\n");
        for (i = 0; i < bbs_count; i++) {
            char tag[5];
            tag[0] = ' '; tag[1] = ' '; tag[2] = (char)('1' + i); tag[3] = ')';
            tag[4] = 0;
            local_print(tag);
            local_print(" ");
            local_print(bbs_name[i][0] ? bbs_name[i] : bbs_addr[i]);
            local_print("\r\n");
        }
        local_print("  0) Enter address\r\nPick: ");
        k = INCH();
        if (k == 0x1B) { local_print("\r\n"); return; }     /* ESC cancels */
        if (k >= '1' && k < '1' + bbs_count) {
            ansi_byte((unsigned char)k);
            local_print("\r\n");
            dial_addr(bbs_addr[k - '1']);
            return;
        }
        local_print("\r\n");                                /* '0'/other -> manual */
    }
    local_print("Dial: ");
    local_line(host, sizeof(host));
    if (host[0]) dial_addr(host);
}

static void do_hangup(void)
{
    if (!online) return;      /* nothing to hang up (offline ATH would be ERROR) */
    serial_print("+++");      /* escape to command mode */
    serial_print("ATH");      /* then hang up */
    acia_put(0x0D);
    online = 0;
}

/* ================= XMODEM file transfer (^S send / ^R receive) ============= */
/* CRC-16/XMODEM (poly 0x1021, init 0); cc65 int is 16-bit so this wraps right. */
static unsigned int crc16(unsigned char *p, int n)
{
    unsigned int c = 0;
    int i, b;
    for (i = 0; i < n; i++) {
        c ^= (unsigned int)p[i] << 8;
        for (b = 0; b < 8; b++)
            c = (c & 0x8000) ? (unsigned int)((c << 1) ^ 0x1021) : (unsigned int)(c << 1);
    }
    return c;
}

/* Read one byte with a crude busy-poll timeout (no 6502 time source); the host
   modem delivers over TCP near-instantly, so this just bounds protocol stalls. */
static int acia_get_timed(unsigned int tries)
{
    int c;
    do { c = acia_get(); if (c >= 0) return c; } while (--tries);
    return -1;
}

static void send_can(void) { acia_put(X_CAN); acia_put(X_CAN); }

/* Local ESC aborts a transfer (other keys are ignored mid-transfer). */
static int poll_esc(void) { return INCH_NB() == 0x1B; }

/* One-line reverse-video status on the bottom row (row 24). */
static void xstatus(const char *s)
{
    int i = 0;
    vaddr(24 * COLS);
    vattr(ATTR_DEFAULT | ATTR_REVERSE);
    while (*s && i < COLS) { vputc((unsigned char)*s++); i++; }
    while (i < COLS) { vputc(' '); i++; }
    vattr(ATTR_DEFAULT);
}

static void xstatus_blk(const char *label, unsigned int n)
{
    char buf[40]; char tmp[6];
    int i = 0, t = 0;
    while (*label && i < 32) buf[i++] = *label++;
    if (n == 0) tmp[t++] = '0';
    else while (n) { tmp[t++] = (char)('0' + (n % 10)); n /= 10; }
    while (t) buf[i++] = tmp[--t];
    buf[i] = 0;
    xstatus(buf);
}

/* Receive an XMODEM stream into the already-open write file. 0 = ok, 1 = fail. */
static int xmodem_recv(void)
{
    unsigned char data[128];
    unsigned int blocknum = 1;
    int crc = 1;                 /* request CRC mode first */
    int started = 0, errcount = 0;
    int c, i;

    for (;;) {
        if (poll_esc()) { send_can(); return 1; }
        if (!started) acia_put(crc ? 'C' : X_NAK);   /* poke the sender */

        c = acia_get_timed(50000u);
        if (c < 0) {                                 /* timeout */
            if (++errcount >= 10) {
                if (!started && crc) { crc = 0; errcount = 0; continue; } /* fall back */
                send_can(); return 1;
            }
            if (started) acia_put(X_NAK);
            continue;
        }
        if (c == X_EOT) { acia_put(X_ACK); return 0; }
        if (c == X_CAN) return 1;
        if (c != X_SOH) continue;                    /* junk: keep waiting */

        started = 1;
        {
            int b1 = acia_get_timed(20000u);
            int b2 = acia_get_timed(20000u);
            int bad = (b1 < 0 || b2 < 0);
            unsigned int rcrc = 0; unsigned char rsum = 0;
            for (i = 0; i < 128; i++) { int d = acia_get_timed(20000u); if (d < 0) bad = 1; data[i] = (unsigned char)d; }
            if (crc) {
                int h = acia_get_timed(20000u), l = acia_get_timed(20000u);
                if (h < 0 || l < 0) bad = 1;
                rcrc = ((unsigned int)(unsigned char)h << 8) | (unsigned char)l;
            } else {
                int s = acia_get_timed(20000u); if (s < 0) bad = 1; rsum = (unsigned char)s;
            }

            if (bad || (unsigned char)(b1 ^ b2) != 0xFF) { acia_put(X_NAK); errcount++; continue; }
            if (crc) { if (crc16(data, 128) != rcrc) { acia_put(X_NAK); errcount++; continue; } }
            else { unsigned char cs = 0; for (i = 0; i < 128; i++) cs += data[i]; if (cs != rsum) { acia_put(X_NAK); errcount++; continue; } }

            errcount = 0;
            if ((unsigned char)b1 == (unsigned char)blocknum) {
                for (i = 0; i < 128; i++) dputb((char)data[i]);
                blocknum = (blocknum + 1) & 0xFF;
                xstatus_blk("XMODEM RECV  block ", blocknum);
                acia_put(X_ACK);
            } else if ((unsigned char)b1 == (unsigned char)(blocknum - 1)) {
                acia_put(X_ACK);             /* duplicate: re-ack, don't rewrite */
            } else {
                send_can(); return 1;        /* out of sequence: fatal */
            }
        }
    }
}

/* Send the already-open read file as an XMODEM stream. 0 = ok, 1 = fail. */
static int xmodem_send(void)
{
    unsigned char data[128];
    unsigned int blocknum = 1;
    int crc = 0, c, i, n, retries, eof = 0;

    /* wait for the receiver to start: 'C' (CRC) or NAK (checksum) */
    retries = 0;
    for (;;) {
        if (poll_esc()) { send_can(); return 1; }
        c = acia_get_timed(60000u);
        if (c == 'C') { crc = 1; break; }
        if (c == X_NAK) { crc = 0; break; }
        if (c == X_CAN) return 1;
        if (++retries >= 12) { send_can(); return 1; }
    }

    for (;;) {
        n = 0;
        while (n < 128) { int d = dgetb(); if (d < 0) { eof = 1; break; } data[n++] = (unsigned char)d; }
        if (n == 0 && eof) break;                    /* exact multiple: nothing left */
        while (n < 128) data[n++] = X_PAD;           /* pad a short final block */

        retries = 0;
        for (;;) {
            if (poll_esc()) { send_can(); return 1; }
            acia_put(X_SOH);
            acia_put((unsigned char)blocknum);
            acia_put((unsigned char)~blocknum);
            for (i = 0; i < 128; i++) acia_put(data[i]);
            if (crc) { unsigned int k = crc16(data, 128); acia_put((unsigned char)(k >> 8)); acia_put((unsigned char)(k & 0xFF)); }
            else { unsigned char cs = 0; for (i = 0; i < 128; i++) cs += data[i]; acia_put(cs); }

            c = acia_get_timed(60000u);
            if (c == X_ACK) break;
            if (c == X_CAN) return 1;
            if (++retries >= 10) { send_can(); return 1; }
            /* NAK or timeout: resend the block */
        }
        xstatus_blk("XMODEM SEND  block ", blocknum);
        blocknum = (blocknum + 1) & 0xFF;
        if (eof) break;
    }

    retries = 0;                                     /* end of transfer */
    for (;;) {
        acia_put(X_EOT);
        c = acia_get_timed(60000u);
        if (c == X_ACK) return 0;
        if (++retries >= 10) return 1;
    }
}

static void do_recv(void)
{
    char name[16];
    local_print("\r\nReceive as: ");
    local_line(name, sizeof(name));
    if (!name[0]) return;
    if (dopen_write(name)) { local_print("Open failed\r\n"); return; }
    xstatus("XMODEM RECEIVE  (ESC aborts)");
    if (xmodem_recv()) { dclose(); local_print("\r\nTransfer failed\r\n"); }
    else { dclose(); local_print("\r\nReceived OK\r\n"); }
    xstatus("");
}

static void do_send(void)
{
    char name[16];
    local_print("\r\nSend file: ");
    local_line(name, sizeof(name));
    if (!name[0]) return;
    if (dopen_read(name)) { local_print("Not found\r\n"); return; }
    xstatus("XMODEM SEND  (ESC aborts)");
    if (xmodem_send()) local_print("\r\nTransfer failed\r\n");
    else local_print("\r\nSent OK\r\n");
    dclose();
    xstatus("");
}

int main(void)
{
    int b, k;

    acia_init();
    while (acia_get() >= 0) { }   /* flush any stale RX from a prior session */
    vfill(' '); vcmd(VCMD_CLEAR);
    cx = 0; cy = 0; attr = ATTR_DEFAULT; vattr(attr); move_cursor();
    local_print("MFC TERM v1.0   ^D dial  ^S send  ^R recv  ^X hang up  ^Q quit\r\n\n");

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
            else if (k == KEY_SEND) { do_send(); }
            else if (k == KEY_RECV) { do_recv(); }
            else if (k == KEY_HANGUP) { do_hangup(); }
            else acia_put((unsigned char)k);   /* forward to the BBS */
        }
    }
    return 0;
}
