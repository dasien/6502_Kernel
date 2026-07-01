/*
 *  IRC -- a minimal IRC chat client for MFC-DOS, over the emulated modem.
 *
 *  Transport reuses the same stack as TERM: keystrokes/bytes go out the 6551
 *  ACIA to the host modem, which bridges to a TCP socket (telnet-aware, but IRC
 *  servers speak raw text so the IAC path is a no-op here). Dialing is in-band
 *  Hayes: we send "ATDT host:port" and wait for the modem's CONNECT result.
 *
 *  UI: a scrolling chat region (rows 0..22), a pinned reverse-video input line
 *  (row 23), and a status bar at the very bottom (row 24: nick, channel,
 *  online/offline) -- so async incoming messages never clobber what you type.
 *
 *  Protocol (subset): PING->PONG keepalive; PRIVMSG as "<nick> text" (CTCP
 *  ACTION as "* nick ..."; CTCP VERSION/PING answered); NOTICE as "-nick- ...";
 *  JOIN/PART/QUIT/NICK as "* nick ..." events; nick-in-use (433) appends '_' and
 *  retries; NO CARRIER/ERROR mark offline. mIRC formatting is honoured where the
 *  display can express it: colour/bold/reverse render; underline/italic drop.
 *  Input starting with '/' is a command (/join /part /nick /msg /me /list /names
 *  /whois /raw /server /quit); anything else is a PRIVMSG to the current channel.
 *  /server drops the carrier and returns to the dial screen; /quit and Ctrl-Q
 *  exit to DOS; ESC quits from the setup prompts.
 *
 *  Scrollback: every chat row is also kept in a RAM history ring (scrollback.c);
 *  PgUp/PgDn page back through it, Home/End jump to the oldest/live tail. While
 *  reviewing, the screen holds still and incoming lines queue below (the status
 *  bar shows "[review +N]"); typing or End snaps back to the live tail.
 */

#include <string.h>
#include "scrollback.h"

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
extern void vscrollbot(unsigned char row);/* scroll-region bottom row (rows 0..row) */
extern void acia_init(void);
extern int  acia_get(void);               /* non-blocking: byte 0..255, or -1 */
extern void acia_put(unsigned char c);
extern char dopen_read(char *name);       /* DOS FAT16 read: 0 = ok, 1 = error */
extern int  dgetb(void);                  /* next byte 0..255, or -1 at EOF */
extern void dclose(void);

#define COLS         80
#define CHATROWS     23            /* rows 0..22 chat; row 23 input; row 24 status */
#define INPUTROW     23
#define STATUSROW    24
#define ATTR_DEFAULT 0x02          /* green on black */
#define ATTR_REVERSE 0x80
#define ATTR_BRIGHT  0x40
#define ATTR_STATUS  0x86          /* reverse + cyan: a black-on-cyan status bar */
#define VCMD_CLEAR   1
#define VCMD_SCROLLUP 2
#define KEY_QUIT     0x11          /* Ctrl-Q */
#define KEY_ESC      0x1B
#define CTCP         0x01          /* CTCP delimiter (\001) */
#define MSGMAX       256           /* max built display line (wraps at COLS) */
#define BOTTOM_ROW_CELL 1760u       /* (CHATROWS-1)*COLS = 22*80: bottom chat row, col 0 */

/* Session outcome: keep chatting, redial (/server), or exit to DOS (/quit, ^Q). */
#define SESS_RUN     0
#define SESS_REDIAL  1
#define SESS_EXIT    2

/* Live chat scrolls in the VIC screen buffer (rows 0..CHATROWS-1) in hardware;
   a copy of every row is also pushed to the scrollback ring (scrollback.c) so
   the user can page back through history. */
static char input[160];                 /* the input line being typed */
static int  inlen;
static char rxline[512];                /* one incoming IRC line being assembled */
static int  rxlen;
static char nick[24];
static char chan[34];                   /* current channel ("" = none joined) */
static char online;                     /* 1 once the modem reports CONNECT */
static int  g_session;                  /* SESS_RUN / SESS_REDIAL / SESS_EXIT */

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

/* Drop the carrier: escape to the modem's command mode and hang up. */
static void hangup(void) { aputs("+++ATH"); acrlf(); online = 0; }

/* Write s at a cell, then pad with spaces out to `pad` columns (clears stale). */
static void put_at(unsigned int cell, const char *s, int pad)
{
    int i = 0;
    vaddr(cell);
    while (*s && i < COLS) { vputc((unsigned char)*s++); i++; }
    while (i < pad)        { vputc(' '); i++; }
}

static void status_repaint(void);            /* defined below; used by chat_add */

/* ---- chat region --------------------------------------------------------- */
/* IRC colour (0-15) -> our 16-colour palette index (fg 0-7, +8 = bright). Order:
   white,black,blue,green,red,brown,purple,orange,yellow,ltgreen,cyan,ltcyan,
   ltblue,pink,grey,ltgrey. */
static const unsigned char irc16[16] = {
    15, 0, 4, 2, 9, 3, 5, 3, 11, 10, 6, 14, 12, 13, 8, 7
};

/* Fold a palette fg (0-15) + bg (0-7) + bold/reverse flags into an attribute
   byte [R][BR][bg:3][fg:3]. Backgrounds have no bright bit. */
static unsigned char compose_attr(unsigned char fg16, unsigned char bg8, int bold, int rev)
{
    unsigned char a = (fg16 & 7) | ((bg8 & 7) << 3);
    if ((fg16 & 8) || bold) a |= ATTR_BRIGHT;
    if (rev) a |= ATTR_REVERSE;
    return a;
}

/* Show one wrapped segment (n cells) as the newest chat line: the VIC hardware-
   scrolls the chat region (rows 0..CHATROWS-1) up by one and blanks the bottom
   row (in the default attribute), then we paint just this line there. So a new
   line costs one chip scroll + ~n cell writes instead of a full-screen repaint,
   and it scrolls smoothly. The pinned input/status rows (23-24) are below the
   scroll region and never move. */
static void chat_line(const char *chars, const unsigned char *attrs, int n)
{
    int j;
    unsigned char last = ATTR_DEFAULT;
    vattr(ATTR_DEFAULT);                     /* blank the vacated row in the default colour */
    vcmd(VCMD_SCROLLUP);
    vaddr(BOTTOM_ROW_CELL);
    for (j = 0; j < n; j++) {
        if (attrs[j] != last) { vattr(attrs[j]); last = attrs[j]; }
        vputc((unsigned char)chars[j]);
    }
}

/* Down-convert UTF-8 to our CP437/ASCII cell set: IRC is UTF-8, but our display
   is one glyph per byte, so raw multibyte text shows as garbage (e.g. a U+00A0
   nbsp arrives as the two glyphs "C2 A0"). Decode each UTF-8 code point to a
   single cell: common typographic punctuation folds to ASCII, other multibyte
   code points become one '?', and bytes that aren't valid UTF-8 pass through as
   CP437 (so genuine high-bit CP437 still shows). */
static char utf8_fold(unsigned int cp)
{
    switch (cp) {
    case 0x00A0: return ' ';                     /* non-breaking space */
    case 0x2018: case 0x2019: return '\'';        /* curly single quotes */
    case 0x201C: case 0x201D: return '"';         /* curly double quotes */
    case 0x2013: case 0x2014: return '-';         /* en/em dash */
    case 0x2026: return '.';                      /* ellipsis */
    default:     return '?';
    }
}

/* Append one display line. mIRC formatting is honoured where our attribute model
   can express it: colour (^C fg[,bg]) -> palette, bold (^B) -> bright, reverse
   (^V) -> reverse, reset (^O) -> default; underline/italic and other control
   codes have no representation and are dropped. The text is UTF-8 down-converted,
   runs of spaces are collapsed (IRC topics pad heavily), then it is wrapped at
   COLS and each row is scrolled onto the screen via chat_line(). */
static void chat_add(const char *s)
{
    static char clean[MSGMAX];
    static unsigned char cbuf[MSGMAX];
    int ci = 0, off, n;
    const char *p;
    unsigned char fg16 = ATTR_DEFAULT & 7, bg8 = (ATTR_DEFAULT >> 3) & 7;
    int bold = 0, rev = 0;
    unsigned char cur = ATTR_DEFAULT;

    while (*s && ci < MSGMAX - 1) {
        unsigned char b = (unsigned char)*s++;
        if (b == 0x03) {                          /* colour: ^C[fg[,bg]] */
            if (*s >= '0' && *s <= '9') {
                int fg = 0, k = 0;
                while (*s >= '0' && *s <= '9' && k < 2) { fg = fg * 10 + (*s++ - '0'); k++; }
                fg16 = irc16[fg & 15];
                if (*s == ',' && s[1] >= '0' && s[1] <= '9') {
                    int bg = 0; s++; k = 0;
                    while (*s >= '0' && *s <= '9' && k < 2) { bg = bg * 10 + (*s++ - '0'); k++; }
                    bg8 = irc16[bg & 15] & 7;
                }
            } else { fg16 = ATTR_DEFAULT & 7; bg8 = (ATTR_DEFAULT >> 3) & 7; }
            cur = compose_attr(fg16, bg8, bold, rev);
            continue;
        }
        if (b == 0x04) {                          /* hex colour ^D: no palette match */
            int k = 0;
            while (k < 6 && ((*s >= '0' && *s <= '9') ||
                             ((*s | 0x20) >= 'a' && (*s | 0x20) <= 'f'))) { s++; k++; }
            continue;
        }
        if (b == 0x02) { bold ^= 1; cur = compose_attr(fg16, bg8, bold, rev); continue; }  /* bold */
        if (b == 0x16) { rev  ^= 1; cur = compose_attr(fg16, bg8, bold, rev); continue; }  /* reverse */
        if (b == 0x0F) {                          /* reset */
            fg16 = ATTR_DEFAULT & 7; bg8 = (ATTR_DEFAULT >> 3) & 7; bold = rev = 0;
            cur = ATTR_DEFAULT; continue;
        }
        if (b < 0x20 || b == 0x7F) continue;      /* underline/italic/other: drop */
        {
            unsigned char oc;
            if (b < 0xC0) oc = b;                  /* ASCII, or stray high byte */
            else {                                 /* UTF-8 lead byte */
                int extra = (b < 0xE0) ? 1 : (b < 0xF0) ? 2 : 3;
                unsigned int cp = b & (0x3Fu >> extra);
                while (extra-- && ((unsigned char)*s & 0xC0) == 0x80)
                    cp = (cp << 6) | ((unsigned char)*s++ & 0x3F);
                oc = (unsigned char)utf8_fold(cp);
            }
            /* Collapse runs of spaces: IRC topics pad with long space runs that
               otherwise wrap into ragged, mostly-blank lines. */
            if (oc == ' ' && ci > 0 && clean[ci - 1] == ' ') continue;
            clean[ci] = (char)oc; cbuf[ci] = cur; ci++;
        }
    }
    clean[ci] = 0;

    p = clean; off = 0;
    do {                                          /* wrap at COLS; one history row per line */
        n = 0;
        while (p[n] && n < COLS) n++;
        sb_push(p, cbuf + off, (unsigned char)n); /* always record in scrollback */
        if (!sb_reviewing()) chat_line(p, cbuf + off, n); /* live: hardware-scroll it in */
        p += n; off += n;
    } while (*p);
    if (sb_reviewing()) status_repaint();         /* update the "[review +N]" count */
}

/* ---- status line (row 24): nick, channel, connection state --------------- */
static void status_repaint(void)
{
    char bar[COLS + 1];
    int j = 0, m;
    const char *st = online ? "[online]" : "[offline]";
    for (m = 0; "MFC IRC "[m] && j < COLS; m++) bar[j++] = "MFC IRC "[m];
    for (m = 0; nick[m] && j < COLS; m++) bar[j++] = nick[m];
    if (j < COLS) bar[j++] = ' ';
    if (chan[0]) { for (m = 0; chan[m] && j < COLS; m++) bar[j++] = chan[m]; }
    else         { for (m = 0; "(no channel)"[m] && j < COLS; m++) bar[j++] = "(no channel)"[m]; }
    if (j < COLS) bar[j++] = ' ';
    for (m = 0; st[m] && j < COLS; m++) bar[j++] = st[m];
    if (sb_reviewing()) {                        /* "[review +N]" while scrolled back */
        unsigned int below = sb_backlog();
        char num[6]; int t = 0;
        for (m = 0; " [review" [m] && j < COLS; m++) bar[j++] = " [review"[m];
        if (below) {                             /* +N new lines waiting below */
            if (j < COLS) bar[j++] = ' ';
            if (j < COLS) bar[j++] = '+';
            do { num[t++] = (char)('0' + below % 10); below /= 10; } while (below && t < 5);
            while (t > 0 && j < COLS) bar[j++] = num[--t];
        }
        if (j < COLS) bar[j++] = ']';
    }
    bar[j] = 0;
    vattr(ATTR_STATUS);
    put_at((unsigned int)STATUSROW * COLS, bar, COLS);
    vattr(ATTR_DEFAULT);
}

/* ---- input line (pinned, reverse video, on row 23) ----------------------- */
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
/* Append a null-terminated string to line[] at *j, capped at COLS. */
static void ln_puts(char *line, int *j, const char *s)
{
    while (*s && *j < MSGMAX - 1) line[(*j)++] = *s++;
}
/* Null-terminate the first whitespace-delimited word of p in place; return p. */
static char *word(char *p)
{
    char *q = p;
    while (*q && *q != ' ') q++;
    *q = 0;
    return p;
}

/* Show a server numeric's human text. `body` is the line past ":server NNN
   target", i.e. the middle params followed by an optional ":trailing". We copy
   the params verbatim and drop only the single ':' that introduces the trailing
   text, so counts/tokens (e.g. "5 operator(s) online", ISUPPORT tokens) survive
   instead of being replaced by the boilerplate trailing alone. */
static void emit_server_text(const char *body)
{
    static char line[MSGMAX];
    int j = 0, tokstart = 1, trailing = 0;
    while (*body && j < MSGMAX - 1) {
        char ch = *body++;
        if (!trailing && tokstart && ch == ':') { trailing = 1; tokstart = 0; continue; }
        if (!trailing) tokstart = (ch == ' ');
        line[j++] = ch;
    }
    line[j] = 0;
    chat_add(line);
}

static void handle_line(char *s)
{
    int i = 0, ns = 0, ne = 0, j, m;
    char *cmd, *t, *args;
    char nk[24]; static char line[MSGMAX];

    if (!s[0]) return;

    if (strncmp(s, "PING", 4) == 0) {           /* keepalive: PING ... -> PONG ... */
        aputs("PONG"); aputs(s + 4); acrlf();
        return;
    }
    /* Link status lines from the modem (no ':' prefix). */
    if (strstr(s, "NO CARRIER")) { online = 0; status_repaint(); chat_add("* disconnected (NO CARRIER)"); return; }
    if (strncmp(s, "ERROR", 5) == 0) { online = 0; status_repaint(); chat_add(s); return; }

    if (s[0] == ':') {                          /* ":nick!user@host CMD ..." */
        i = 1; ns = 1;
        while (s[i] && s[i] != ' ' && s[i] != '!') i++;
        ne = i;                                 /* nick = s[ns..ne) */
        while (s[i] && s[i] != ' ') i++;        /* skip rest of the prefix */
        if (s[i] == ' ') i++;
    }
    cmd = s + i;
    args = cmd;                                 /* first argument word */
    while (*args && *args != ' ') args++;
    if (*args == ' ') args++;
    t = strstr(cmd, " :");                      /* trailing text (after " :") */
    if (t) t += 2;
    for (m = ns, j = 0; m < ne && j < 23; m++) nk[j++] = s[m];
    nk[j] = 0;

    if (strncmp(cmd, "PRIVMSG", 7) == 0) {
        if (t && t[0] == CTCP) {                 /* CTCP: \001TAG args\001 */
            char *body = t + 1, *end = strchr(body, CTCP);
            if (end) *end = 0;
            if (strncmp(body, "ACTION", 6) == 0) {
                j = 0; ln_puts(line, &j, "* "); ln_puts(line, &j, nk);
                ln_puts(line, &j, " "); ln_puts(line, &j, body[6] ? body + 7 : "");
                line[j] = 0; chat_add(line);
            } else if (strncmp(body, "VERSION", 7) == 0) {
                aputs("NOTICE "); aputs(nk); aputs(" :\001VERSION MFC IRC 1.4\001"); acrlf();
            } else if (strncmp(body, "PING", 4) == 0) {
                aputs("NOTICE "); aputs(nk); aputs(" :\001PING");
                aputs(body + 4); acia_put(CTCP); acrlf();
            }
            return;
        }
        if (!t) return;
        j = 0; ln_puts(line, &j, "<"); ln_puts(line, &j, nk);
        ln_puts(line, &j, "> "); ln_puts(line, &j, t);
        line[j] = 0; chat_add(line);
        return;
    }
    if (strncmp(cmd, "NOTICE", 6) == 0) {
        j = 0;
        if (nk[0]) { ln_puts(line, &j, "-"); ln_puts(line, &j, nk); ln_puts(line, &j, "- "); }
        else       { ln_puts(line, &j, "* "); }   /* unprefixed AUTH/server notice */
        ln_puts(line, &j, t ? t : "");
        line[j] = 0; chat_add(line);
        return;
    }
    if (strncmp(cmd, "JOIN", 4) == 0) {
        j = 0; ln_puts(line, &j, "* "); ln_puts(line, &j, nk);
        ln_puts(line, &j, " joined "); ln_puts(line, &j, word(t ? t : args));
        line[j] = 0; chat_add(line);
        return;
    }
    if (strncmp(cmd, "PART", 4) == 0) {
        j = 0; ln_puts(line, &j, "* "); ln_puts(line, &j, nk);
        ln_puts(line, &j, " left "); ln_puts(line, &j, word(args));
        line[j] = 0; chat_add(line);
        return;
    }
    if (strncmp(cmd, "QUIT", 4) == 0) {
        j = 0; ln_puts(line, &j, "* "); ln_puts(line, &j, nk);
        ln_puts(line, &j, " quit");
        if (t) { ln_puts(line, &j, " ("); ln_puts(line, &j, t); ln_puts(line, &j, ")"); }
        line[j] = 0; chat_add(line);
        return;
    }
    if (strncmp(cmd, "NICK", 4) == 0) {
        j = 0; ln_puts(line, &j, "* "); ln_puts(line, &j, nk);
        ln_puts(line, &j, " is now "); ln_puts(line, &j, word(t ? t : args));
        line[j] = 0; chat_add(line);
        return;
    }
    if (cmd[0] >= '0' && cmd[0] <= '9') {        /* server numeric reply */
        char *p;
        if (strncmp(cmd, "001", 3) == 0) { online = 1; status_repaint(); }
        if (strncmp(cmd, "433", 3) == 0) {       /* nick in use -> append '_', retry */
            int nl = 0; while (nick[nl]) nl++;
            if (nl < (int)sizeof(nick) - 2) { nick[nl] = '_'; nick[nl + 1] = 0; }
            aputs("NICK "); aputs(nick); acrlf();
            status_repaint();
            j = 0; ln_puts(line, &j, "* nick in use, trying "); ln_puts(line, &j, nick);
            line[j] = 0; chat_add(line);
            return;
        }
        p = args;                                /* args -> target; skip it to the body */
        while (*p && *p != ' ') p++;
        if (*p == ' ') p++;
        emit_server_text(p);                     /* params + trailing, prefix stripped */
        return;
    }
    chat_add(t ? t : s);                         /* anything else: trailing or raw */
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
    static char line[MSGMAX];
    int j = 0, m = 0;
    line[j++] = '<';
    while (nick[m] && j < MSGMAX - 1) line[j++] = nick[m++];
    if (j < MSGMAX - 1) line[j++] = '>';
    if (j < MSGMAX - 1) line[j++] = ' ';
    m = 0;
    while (text[m] && j < MSGMAX - 1) line[j++] = text[m++];
    line[j] = 0;
    chat_add(line);
}

/* If the input line is command `c`, optionally followed by " <arg>", return a
   pointer to the argument (past spaces), "" if the command is bare, or NULL if
   the line is not this command. Lets each verb validate its own argument. */
static const char *cmd_arg(const char *c)
{
    int n = 0;
    while (c[n]) n++;
    if (strncmp(input, c, n) != 0) return 0;
    if (input[n] == 0) return input + n;             /* bare command -> "" */
    if (input[n] != ' ') return 0;                   /* e.g. "/parted" != "/part" */
    { const char *p = input + n + 1; while (*p == ' ') p++; return p; }
}

static void send_input(void)
{
    const char *a;
    if (inlen == 0) return;

    if (input[0] != '/') {                           /* plain text -> current channel */
        if (chan[0]) { aputs("PRIVMSG "); aputs(chan); aputs(" :"); aputs(input); acrlf(); echo_self(input); }
        else chat_add("(no channel - use /join #channel)");
    } else if ((a = cmd_arg("/quit"))) {
        aputs("QUIT :MFC IRC"); acrlf();
        g_session = SESS_EXIT;                       /* main hangs up + returns to DOS */
    } else if ((a = cmd_arg("/server"))) {
        aputs("QUIT :changing servers"); acrlf();
        g_session = SESS_REDIAL;                     /* main hangs up + returns to dial screen */
    } else if ((a = cmd_arg("/join"))) {
        if (*a) { aputs("JOIN "); aputs(a); acrlf();
                  copy_word(chan, a, (int)sizeof(chan)); status_repaint(); }
        else chat_add("* usage: /join #channel");
    } else if ((a = cmd_arg("/part"))) {
        const char *ch = *a ? a : (const char *)chan;              /* bare = leave current channel */
        if (ch[0]) { aputs("PART "); aputs(ch); acrlf();
                     if (!*a) { chan[0] = 0; status_repaint(); } }
        else chat_add("* not in a channel");
    } else if ((a = cmd_arg("/nick"))) {
        if (*a) { aputs("NICK "); aputs(a); acrlf();
                  copy_word(nick, a, (int)sizeof(nick)); status_repaint(); }
        else chat_add("* usage: /nick <name>");
    } else if ((a = cmd_arg("/me"))) {
        if (*a && chan[0]) {
            aputs("PRIVMSG "); aputs(chan); aputs(" :\001ACTION "); aputs(a); acia_put(CTCP); acrlf();
            { static char ln[MSGMAX]; int j = 0, m = 0;
              ln[j++] = '*'; if (j < MSGMAX - 1) ln[j++] = ' ';
              while (nick[m] && j < MSGMAX - 1) ln[j++] = nick[m++];
              if (j < MSGMAX - 1) ln[j++] = ' ';
              m = 0; while (a[m] && j < MSGMAX - 1) ln[j++] = a[m++];
              ln[j] = 0; chat_add(ln); }
        } else chat_add("* usage: /me <action> (in a channel)");
    } else if ((a = cmd_arg("/msg"))) {
        char tgt[24]; int i = 0; const char *p = a;
        while (*p && *p != ' ' && i < 23) tgt[i++] = *p++;
        tgt[i] = 0;
        while (*p == ' ') p++;
        if (tgt[0] && *p) {
            aputs("PRIVMSG "); aputs(tgt); aputs(" :"); aputs(p); acrlf();
            { static char ln[MSGMAX]; int j = 0, m = 0;
              ln[j++] = '>'; while (tgt[m] && j < MSGMAX - 1) ln[j++] = tgt[m++];
              if (j < MSGMAX - 1) ln[j++] = '<'; if (j < MSGMAX - 1) ln[j++] = ' ';
              m = 0; while (p[m] && j < MSGMAX - 1) ln[j++] = p[m++];
              ln[j] = 0; chat_add(ln); }
        } else chat_add("* usage: /msg <nick> <text>");
    } else if ((a = cmd_arg("/list"))) {
        aputs("LIST"); if (*a) { acia_put(' '); aputs(a); } acrlf();
        chat_add("* listing channels (this can be long; filter e.g. /list >50)");
    } else if ((a = cmd_arg("/names"))) {
        const char *ch = *a ? a : (const char *)chan;
        if (ch[0]) { aputs("NAMES "); aputs(ch); acrlf(); }
        else chat_add("* usage: /names #channel");
    } else if ((a = cmd_arg("/whois"))) {
        if (*a) { aputs("WHOIS "); aputs(a); acrlf(); }
        else chat_add("* usage: /whois <nick>");
    } else if ((a = cmd_arg("/raw"))) {
        if (*a) { aputs(a); acrlf(); }
        else chat_add("* usage: /raw <irc command>");
    } else {
        chat_add("* unknown (/join /part /nick /msg /me /list /names /whois /raw /server /quit)");
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

/* Read a line at a screen cell, echoing as typed. Returns 0 on RETURN, or -1 if
   ESC cancelled (the caller treats that as "quit the app"). */
static int local_read(unsigned int cell, char *buf, int max)
{
    int len = 0, k;
    vcursor(cell);
    for (;;) {
        k = INCH();
        if (k == KEY_ESC) { buf[0] = 0; return -1; }
        if (k == 0x0D || k == 0x0A) { buf[len] = 0; return 0; }
        if ((k == 0x08 || k == 0x7F) && len) {
            len--; vaddr(cell + len); vputc(' '); vcursor(cell + len); continue;
        }
        if (k >= 0x20 && k < 0x7F && len < max - 1) {
            buf[len] = (char)k; vaddr(cell + len); vputc((unsigned char)k);
            len++; vcursor(cell + len);
        }
    }
}

/* Prompt for server/nick/channel and dial. Returns 0 once connected+registered,
   or 1 if the user pressed ESC to quit. Retries the whole flow if a dial fails
   or is cancelled, so a bad address doesn't strand the user on "Connecting...". */
static int setup(void)
{
    char server[80];
    int row, i, k;

    for (;;) {                                  /* server-pick + connect, retry on failure */
        vfill(' '); vcmd(VCMD_CLEAR);
        vattr(ATTR_DEFAULT);
        put_at(0 * COLS, "MFC IRC v1.4   (ESC quits)", COLS);
        row = 2;
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
            if (k == KEY_ESC) return 1;
            if (k >= '1' && k < '1' + srv_count) strcpy(server, srv_addr[k - '1']);
        }
        if (!server[0]) {
            put_at((unsigned int)row * COLS, "Server:   ", 0);
            if (local_read((unsigned int)row++ * COLS + 10, server, (int)sizeof(server)) < 0) return 1;
            if (!server[0]) strcpy(server, "irc.libera.chat:6667");
        }
        put_at((unsigned int)row * COLS, "Nick:     ", 0);
        if (local_read((unsigned int)row++ * COLS + 10, nick, (int)sizeof(nick)) < 0) return 1;
        if (!nick[0]) strcpy(nick, "mfcuser");
        put_at((unsigned int)row * COLS, "Channel:  ", 0);
        if (local_read((unsigned int)row++ * COLS + 10, chan, (int)sizeof(chan)) < 0) return 1;

        put_at((unsigned int)(row + 1) * COLS, "Connecting... (ESC cancels)", COLS);
        online = 0;
        aputs("ATDT "); aputs(server); acrlf();

        {   /* wait for CONNECT / failure (ERROR|NO CARRIER) / ESC / timeout */
            int sc = 0, se = 0, sn = 0, res = 0;    /* 0 wait, 1 ok, 2 fail, 3 cancel */
            long t = 12000000L;
            while (res == 0) {
                int b = acia_get();
                if (b >= 0) {
                    if (feed_match("CONNECT", &sc, (unsigned char)b)) res = 1;
                    else if (feed_match("ERROR", &se, (unsigned char)b)) res = 2;
                    else if (feed_match("NO CARRIER", &sn, (unsigned char)b)) res = 2;
                } else if (INCH_NB() == KEY_ESC) {
                    res = 3;
                } else if (--t == 0) {
                    res = 2;
                }
            }
            if (res == 1) {
                online = 1;
                aputs("NICK "); aputs(nick); acrlf();
                aputs("USER "); aputs(nick); aputs(" 0 * :"); aputs(nick); acrlf();
                if (chan[0]) { aputs("JOIN "); aputs(chan); acrlf(); }
                return 0;
            }
            hangup();
            put_at((unsigned int)(row + 1) * COLS,
                   res == 3 ? "Cancelled." : "Connection failed.", COLS);
            put_at((unsigned int)(row + 3) * COLS, "Press a key to retry, ESC to quit.", COLS);
            if (INCH() == KEY_ESC) return 1;
        }
    }
}

/* Handle a completed review-navigation key (PgUp/PgDn/Home/End). Returns 1 if it
   was a navigation key (and repaints if the view moved), 0 otherwise. `final` is
   the CSI final byte and `num` the numeric parameter (5=PgUp, 6=PgDn on '~'). */
static int review_nav(int final, int num)
{
    int moved;
    if (final == '~' && num == 5)      moved = sb_pageup();
    else if (final == '~' && num == 6) moved = sb_pagedown();
    else if (final == 'H')             moved = sb_home();
    else if (final == 'F')             moved = sb_end();
    else return 0;                     /* not one of ours (e.g. an arrow) */
    if (moved) { sb_paint(); status_repaint(); }
    return 1;
}

/* Run one connected session; returns SESS_REDIAL (/server) or SESS_EXIT (/quit, ^Q). */
static int chat_session(void)
{
    int b, k;
    int csi = 0, num = 0;                    /* CSI decoder: 0 ground, 1 after ESC, 2 in CSI */

    vattr(ATTR_DEFAULT); vfill(' '); vcmd(VCMD_CLEAR);  /* clear (resets scroll region) */
    vscrollbot(CHATROWS - 1);                /* scroll only the chat rows; pin 23-24 */
    sb_init(0, CHATROWS, COLS);              /* scrollback owns rows 0..CHATROWS-1 */
    sb_reset();
    rxlen = 0; inlen = 0; input[0] = 0;
    status_repaint();
    input_repaint();

    g_session = SESS_RUN;
    while (g_session == SESS_RUN) {
        b = acia_get();
        if (b >= 0) {                           /* each completed line scrolls in immediately */
            if (b == '\n') { rxline[rxlen] = 0; handle_line(rxline); rxlen = 0; }
            else if (b != '\r') { if (rxlen < (int)sizeof(rxline) - 1) rxline[rxlen++] = (char)b; }
            continue;
        }
        k = INCH_NB();
        if (k < 0) continue;

        /* Navigation keys arrive as CSI sequences (ESC [ ... final); decode them
           across loop iterations so PgUp/PgDn/Home/End drive the scrollback and
           don't leak into the input line. */
        if (csi == 1) { csi = (k == '[') ? 2 : 0; num = 0; continue; }
        if (csi == 2) {
            if (k >= '0' && k <= '9') { num = num * 10 + (k - '0'); continue; }
            review_nav(k, num);                 /* final byte: act (or ignore) */
            csi = 0;
            continue;
        }
        if (k == KEY_ESC) { csi = 1; continue; } /* start of a CSI (or a stray ESC) */

        /* Any other key returns us to the live tail before it acts. */
        if (sb_reviewing()) { sb_end(); sb_paint(); status_repaint(); }

        if (k == KEY_QUIT) { aputs("QUIT :MFC IRC"); acrlf(); g_session = SESS_EXIT; }
        else if (k == 0x0D || k == 0x0A) { send_input(); }
        else if (k == 0x08 || k == 0x7F) {
            if (inlen) { inlen--; input[inlen] = 0; input_repaint(); }
        } else if (k >= 0x20 && k < 0x7F && inlen < (int)sizeof(input) - 1) {
            input[inlen++] = (char)k; input[inlen] = 0; input_repaint();
        }
    }
    return g_session;
}

int main(void)
{
    acia_init();

    for (;;) {                                  /* one iteration per server session */
        while (acia_get() >= 0) { }             /* flush stale RX */
        chan[0] = 0; online = 0;
        if (setup()) { QUITDOS(); }             /* ESC in setup -> exit to DOS */
        if (chat_session() == SESS_EXIT) { hangup(); QUITDOS(); }
        hangup();                               /* /server: drop carrier, redial */
    }
    return 0;
}
