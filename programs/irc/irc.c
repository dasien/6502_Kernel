/*
 *  IRC -- a minimal IRC chat client for MFC-DOS, over the emulated modem.
 *
 *  Transport reuses the same stack as TERM: keystrokes/bytes go out the 6551
 *  ACIA to the host modem, which bridges to a TCP socket (telnet-aware, but IRC
 *  servers speak raw text so the IAC path is a no-op here). Dialing is in-band
 *  Hayes: we send "ATDT host:port" and wait for the modem's CONNECT result.
 *
 *  UI: the screen is split into a scrolling chat region (rows 0..23, a 24-line
 *  software ring repainted on each new line) and a pinned reverse-video input
 *  line on row 24 -- so async incoming messages never clobber what you type.
 *
 *  Protocol (subset): PING->PONG keepalive; PRIVMSG shown as "<nick> text";
 *  server numerics / NOTICE / MOTD shown as their trailing text. Input that
 *  starts with '/' is a command (/join, /nick, /me, /raw, /quit); anything else
 *  is sent as PRIVMSG to the current channel and echoed locally. Ctrl-Q quits.
 */

#include <string.h>

/* ---- kernel/VIC/ACIA glue (glue.s, shared shape with TERM) --------------- */
extern char INCH(void);             /* blocking key read */
extern int  INCH_NB(void);          /* non-blocking: key 0..255, or -1 */
extern void QUITDOS(void);          /* clear screen + return to DOS */
extern void vaddr(unsigned int cell);     /* point the VIC data port at a cell */
extern void vputc(unsigned char ch);      /* write a glyph (auto-increments) */
extern void vattr(unsigned char a);       /* color/attribute latch */
extern void vcursor(unsigned int cell);   /* position the hardware cursor */
extern void vfill(unsigned char ch);      /* fill char for chip commands */
extern void vcmd(unsigned char cmd);      /* 1=clear 2=scrollup 3=down 4=fillrow */
extern void acia_init(void);
extern int  acia_get(void);               /* non-blocking: byte 0..255, or -1 */
extern void acia_put(unsigned char c);
extern char dopen_read(char *name);       /* DOS FAT16 read: 0 = ok, 1 = error */
extern int  dgetb(void);                  /* next byte 0..255, or -1 at EOF */
extern void dclose(void);

#define COLS         80
#define CHATROWS     24            /* rows 0..23 are chat; row 24 is input */
#define INPUTROW     24
#define ATTR_DEFAULT 0x02          /* green on black */
#define ATTR_REVERSE 0x80
#define VCMD_CLEAR   1
#define KEY_QUIT     0x11          /* Ctrl-Q */

static char chat[CHATROWS][COLS + 1];   /* the visible chat ring */
static char input[160];                 /* the input line being typed */
static int  inlen;
static char rxline[512];                /* one incoming IRC line being assembled */
static int  rxlen;
static char nick[24];
static char chan[34];                   /* current channel ("" = none joined) */

/* Saved IRC server list (SYSTEM/IRC.LST), same format/parser as TERM's dial-
   list: one entry per line, "host:port  name"; '#' comments/blank lines ignored,
   up to 9 entries offered in the Server menu. Editable in EDIT. */
#define MAXSRV  9
#define SRVBUF  600
static char  srvbuf[SRVBUF];
static char *srv_addr[MAXSRV];
static char *srv_name[MAXSRV];
static int   srv_count;

static void load_server_list(void)
{
    int n = 0, c;
    char *p;
    srv_count = 0;
    if (dopen_read("SYSTEM/IRC.LST")) return;
    while ((c = dgetb()) >= 0 && n < SRVBUF - 1) srvbuf[n++] = (char)c;
    dclose();
    srvbuf[n] = 0;
    p = srvbuf;
    while (*p && srv_count < MAXSRV) {
        char *line = p;
        while (*p && *p != '\n' && *p != '\r') p++;
        if (*p) *p++ = 0;
        while (*p == '\n' || *p == '\r') p++;
        while (*line == ' ' || *line == '\t') line++;
        if (*line == 0 || *line == '#') continue;
        srv_addr[srv_count] = line;
        while (*line && *line != ' ' && *line != '\t') line++;
        if (*line) { *line++ = 0; while (*line == ' ' || *line == '\t') line++; }
        srv_name[srv_count] = line;
        srv_count++;
    }
}

/* ---- low-level output helpers -------------------------------------------- */
static void aputs(const char *s) { while (*s) acia_put((unsigned char)*s++); }
static void acrlf(void)          { acia_put('\r'); acia_put('\n'); }

/* Write s at a cell, then pad with spaces out to `pad` columns (clears stale). */
static void put_at(unsigned int cell, const char *s, int pad)
{
    int i = 0;
    vaddr(cell);
    while (*s && i < COLS) { vputc((unsigned char)*s++); i++; }
    while (i < pad)        { vputc(' '); i++; }
}

/* ---- chat region --------------------------------------------------------- */
static void chat_repaint(void)
{
    int r;
    vattr(ATTR_DEFAULT);
    for (r = 0; r < CHATROWS; r++) put_at((unsigned int)r * COLS, chat[r], COLS);
}

/* Append one display line, wrapping at COLS; scrolls the ring up and repaints. */
static void chat_add(const char *s)
{
    int r, n;
    do {
        for (r = 0; r < CHATROWS - 1; r++) strcpy(chat[r], chat[r + 1]);
        n = 0;
        while (s[n] && n < COLS) { chat[CHATROWS - 1][n] = s[n]; n++; }
        chat[CHATROWS - 1][n] = 0;
        s += n;
    } while (*s);
    chat_repaint();
}

/* ---- input line (pinned, reverse video, on row 24) ----------------------- */
static void input_repaint(void)
{
    int i = 0, j = 0;
    vaddr((unsigned int)INPUTROW * COLS);
    vattr(ATTR_DEFAULT | ATTR_REVERSE);
    vputc('>'); vputc(' '); i = 2;
    while (input[j] && i < COLS) { vputc((unsigned char)input[j++]); i++; }
    while (i < COLS) { vputc(' '); i++; }
    vattr(ATTR_DEFAULT);
    vcursor((unsigned int)INPUTROW * COLS + 2 + inlen);
}

/* ---- incoming IRC line --------------------------------------------------- */
static void handle_line(char *s)
{
    int i = 0, ns = 0, ne = 0;
    char *cmd, *t;
    char line[COLS + 1];
    int j, m;

    if (!s[0]) return;

    if (strncmp(s, "PING", 4) == 0) {           /* keepalive: PING ... -> PONG ... */
        aputs("PONG"); aputs(s + 4); acrlf();
        return;
    }

    if (s[0] == ':') {                          /* ":nick!user@host CMD ..." */
        i = 1; ns = 1;
        while (s[i] && s[i] != ' ' && s[i] != '!') i++;
        ne = i;                                 /* nick = s[ns..ne) */
        while (s[i] && s[i] != ' ') i++;        /* skip rest of the prefix */
        if (s[i] == ' ') i++;
    }
    cmd = s + i;

    if (strncmp(cmd, "PRIVMSG", 7) == 0) {
        t = strstr(cmd, " :");                  /* the message text */
        if (!t) return;
        t += 2;
        j = 0;
        line[j++] = '<';
        m = ns;
        while (m < ne && j < COLS) line[j++] = s[m++];
        if (j < COLS) line[j++] = '>';
        if (j < COLS) line[j++] = ' ';
        while (*t && j < COLS) line[j++] = *t++;
        line[j] = 0;
        chat_add(line);
        return;
    }

    /* Everything else (welcome numerics, NOTICE, MOTD, JOIN/PART...): show the
       trailing text if there is one, otherwise the whole raw line. */
    t = strstr(s, " :");
    chat_add(t ? t + 2 : s);
}

/* ---- outgoing (the input line) ------------------------------------------- */
static void copy_word(char *dst, const char *src, int max)
{
    int i = 0;
    while (src[i] && src[i] != ' ' && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static void echo_self(const char *text)
{
    char line[COLS + 1];
    int j = 0, m = 0;
    line[j++] = '<';
    while (nick[m] && j < COLS) line[j++] = nick[m++];
    if (j < COLS) line[j++] = '>';
    if (j < COLS) line[j++] = ' ';
    m = 0;
    while (text[m] && j < COLS) line[j++] = text[m++];
    line[j] = 0;
    chat_add(line);
}

static void send_input(void)
{
    if (inlen == 0) return;

    if (input[0] == '/') {
        if (strncmp(input, "/quit", 5) == 0) {
            aputs("QUIT :MFC IRC"); acrlf();
            QUITDOS();
        } else if (strncmp(input, "/join ", 6) == 0) {
            aputs("JOIN "); aputs(input + 6); acrlf();
            copy_word(chan, input + 6, (int)sizeof(chan));
        } else if (strncmp(input, "/nick ", 6) == 0) {
            aputs("NICK "); aputs(input + 6); acrlf();
            copy_word(nick, input + 6, (int)sizeof(nick));
        } else if (strncmp(input, "/me ", 4) == 0 && chan[0]) {
            aputs("PRIVMSG "); aputs(chan); aputs(" :\001ACTION ");
            aputs(input + 4); acia_put(0x01); acrlf();
            { char a[COLS + 1]; int j = 0, m = 0;
              a[j++] = '*'; if (j < COLS) a[j++] = ' ';
              while (nick[m] && j < COLS) a[j++] = nick[m++];
              if (j < COLS) a[j++] = ' ';
              m = 4; while (input[m] && j < COLS) a[j++] = input[m++];
              a[j] = 0; chat_add(a); }
        } else if (strncmp(input, "/raw ", 5) == 0) {
            aputs(input + 5); acrlf();
        } else {
            chat_add("(unknown command)");
        }
    } else if (chan[0]) {
        aputs("PRIVMSG "); aputs(chan); aputs(" :"); aputs(input); acrlf();
        echo_self(input);
    } else {
        chat_add("(no channel - use /join #channel)");
    }

    inlen = 0; input[0] = 0;
    input_repaint();
}

/* ---- connect flow -------------------------------------------------------- */
/* Incremental substring matcher (for the modem's CONNECT result code). */
static int feed_match(const char *pat, int *st, unsigned char b)
{
    if (b == (unsigned char)pat[*st]) {
        (*st)++;
        if (pat[*st] == 0) { *st = 0; return 1; }
    } else {
        *st = (b == (unsigned char)pat[0]) ? 1 : 0;
    }
    return 0;
}

/* Read a line at a screen cell, echoing as typed; returns on RETURN. */
static void local_read(unsigned int cell, char *buf, int max)
{
    int len = 0, k;
    vcursor(cell);
    for (;;) {
        k = INCH();
        if (k == 0x0D || k == 0x0A) { buf[len] = 0; return; }
        if ((k == 0x08 || k == 0x7F) && len) {
            len--; vaddr(cell + len); vputc(' '); vcursor(cell + len); continue;
        }
        if (k >= 0x20 && k < 0x7F && len < max - 1) {
            buf[len] = (char)k; vaddr(cell + len); vputc((unsigned char)k);
            len++; vcursor(cell + len);
        }
    }
}

static void setup(void)
{
    char server[80];
    long t;
    int st = 0, b, row = 2, i, k;

    vattr(ATTR_DEFAULT);
    put_at(0 * COLS, "MFC IRC v1.0", COLS);

    /* Server: pick from IRC.LST if present, else type one. */
    server[0] = 0;
    load_server_list();
    if (srv_count) {
        put_at((unsigned int)row++ * COLS, "IRC servers:", COLS);
        for (i = 0; i < srv_count; i++) {
            char ln[COLS + 1];
            const char *nm = srv_name[i][0] ? srv_name[i] : srv_addr[i];
            int j = 0, m = 0;
            ln[j++] = ' '; ln[j++] = ' '; ln[j++] = (char)('1' + i); ln[j++] = ')'; ln[j++] = ' ';
            while (nm[m] && j < COLS) ln[j++] = nm[m++];
            ln[j] = 0;
            put_at((unsigned int)row++ * COLS, ln, COLS);
        }
        put_at((unsigned int)row++ * COLS, "  0) Enter address", COLS);
        put_at((unsigned int)row * COLS, "Pick: ", 0);
        vcursor((unsigned int)row * COLS + 6);
        k = INCH();
        row++;
        if (k >= '1' && k < '1' + srv_count) strcpy(server, srv_addr[k - '1']);
    }
    if (!server[0]) {
        put_at((unsigned int)row * COLS, "Server:   ", 0);
        local_read((unsigned int)row++ * COLS + 10, server, (int)sizeof(server));
        if (!server[0]) strcpy(server, "irc.libera.chat:6667");
    }
    put_at((unsigned int)row * COLS, "Nick:     ", 0);
    local_read((unsigned int)row++ * COLS + 10, nick, (int)sizeof(nick));
    if (!nick[0]) strcpy(nick, "mfcuser");
    put_at((unsigned int)row * COLS, "Channel:  ", 0);
    local_read((unsigned int)row++ * COLS + 10, chan, (int)sizeof(chan));

    put_at((unsigned int)(row + 1) * COLS, "Connecting...", COLS);
    aputs("ATDT "); aputs(server); acrlf();

    for (t = 3000000L; t; ) {                   /* wait for CONNECT (with timeout) */
        b = acia_get();
        if (b >= 0) { if (feed_match("CONNECT", &st, (unsigned char)b)) break; }
        else        { --t; }
    }

    aputs("NICK "); aputs(nick); acrlf();
    aputs("USER "); aputs(nick); aputs(" 0 * :"); aputs(nick); acrlf();
    if (chan[0]) { aputs("JOIN "); aputs(chan); acrlf(); }
}

int main(void)
{
    int b, k, r;

    acia_init();
    while (acia_get() >= 0) { }                 /* flush stale RX */
    vfill(' '); vcmd(VCMD_CLEAR);

    setup();

    for (r = 0; r < CHATROWS; r++) chat[r][0] = 0;
    rxlen = 0; inlen = 0; input[0] = 0;
    chat_repaint();
    input_repaint();

    for (;;) {
        b = acia_get();
        if (b >= 0) {
            if (b == '\n') { rxline[rxlen] = 0; handle_line(rxline); rxlen = 0; }
            else if (b != '\r') { if (rxlen < (int)sizeof(rxline) - 1) rxline[rxlen++] = (char)b; }
            input_repaint();                    /* keep the cursor on the input line */
            continue;
        }

        k = INCH_NB();
        if (k >= 0) {
            if (k == KEY_QUIT) { aputs("QUIT :MFC IRC"); acrlf(); QUITDOS(); }
            else if (k == 0x0D || k == 0x0A) { send_input(); }
            else if (k == 0x08 || k == 0x7F) {
                if (inlen) { inlen--; input[inlen] = 0; input_repaint(); }
            } else if (k >= 0x20 && k < 0x7F && inlen < (int)sizeof(input) - 1) {
                input[inlen++] = (char)k; input[inlen] = 0; input_repaint();
            }
        }
    }
    return 0;
}
