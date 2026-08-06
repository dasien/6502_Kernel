/* ============================================================================
 * frontier.c -- FRONTIER FORTUNE (build step 8: a living economy).
 *
 * A Wild-West trading game: buy low, sell high, beat the loan shark before the
 * 60 days run out. Port of the author's 2008 Objective-C prototype; see
 * DESIGN.md for the full design and for what the prototype left unfinished.
 *
 * Menu-driven and turn-based -- there is no tick loop, no scrolling and no
 * timing anywhere in this program. Each screen paints itself once on entry and
 * then waits for a keystroke.
 *
 * Working here: the status screen, prices with per-town specialities, the
 * general store, the bank, the loan office, the wagon yard, and travel -- which
 * is what gives the run a shape, since every day on the trail compounds the debt
 * and the sixtieth ends the game. Every screen is now live.
 *
 * Events and road agents are rolled on arrival: see arrive() for why the order
 * of event, price roll and holdup matters.
 * ==========================================================================*/
#include "frontier.h"

/* ---- RNG: inline xorshift, seeded from the RTC ---- */
static unsigned int rngv = 0xACE1;
unsigned int rnd16(void) {
    rngv ^= (unsigned int)(rngv << 7);
    rngv ^= (unsigned int)(rngv >> 9);
    rngv ^= (unsigned int)(rngv << 8);
    return rngv;
}
unsigned int rndn(unsigned int n) {
    if (n == 0) return 0;
    return rnd16() % n;
}

/* ============================================================================
 * Screen primitives
 * ==========================================================================*/
static unsigned char last_attr;

static void put_cell(unsigned char g, unsigned char a) {
    if (a != last_attr) { vattr(a); last_attr = a; }
    vputc(g);
}
static void put_str(unsigned char x, unsigned char y, const char *s, unsigned char a) {
    vaddr((unsigned int)y * SCR_W + x);
    last_attr = 0xFF;
    while (*s) put_cell((unsigned char)*s++, a);
}
static void put_rep(unsigned char x, unsigned char y, unsigned char g,
                    unsigned char n, unsigned char a) {
    vaddr((unsigned int)y * SCR_W + x);
    last_attr = 0xFF;
    while (n--) put_cell(g, a);
}
static void clear_row(unsigned char y) {
    put_rep(0, y, ' ', SCR_W, A_TEXT);
}
static void cls(void) {
    vattr(A_TEXT); vaddr(0); vfill(' '); vcmd(VCMD_CLEAR);
    last_attr = 0xFF;
}

/* Format a signed 32-bit money value as "$1,234,567". Returns the length.
 *
 * We format this ourselves rather than using the kernel's K_PRINT_DEC ($FF27):
 * that prints through the kernel cursor, but this program paints positioned
 * cells, so the two would fight over where output lands. cc65 has no long
 * printf either. Thousands separators are worth the few extra bytes -- a bare
 * "3000000" is genuinely hard to read at a glance while trading. */
static unsigned char fmt_money(Money v, char *buf) {
    char tmp[16];
    unsigned char n = 0, len = 0, group = 0;
    unsigned long u;

    if (v < 0) { buf[len++] = '-'; u = (unsigned long)(-v); }
    else       { u = (unsigned long)v; }

    do {
        tmp[n++] = (char)('0' + (unsigned char)(u % 10));
        u /= 10;
        if (++group == 3 && u) { tmp[n++] = ','; group = 0; }
    } while (u);

    buf[len++] = '$';
    while (n) buf[len++] = tmp[--n];
    buf[len] = 0;
    return len;
}

/* Plain unsigned decimal, no separators (days, counts, unit prices). */
static unsigned char fmt_num(unsigned int v, char *buf) {
    char tmp[6];
    unsigned char n = 0, len = 0;
    do { tmp[n++] = (char)('0' + (v % 10)); v /= 10; } while (v);
    while (n) buf[len++] = tmp[--n];
    buf[len] = 0;
    return len;
}

/* Same, from a 32-bit value -- an input field may hold a money amount. */
static unsigned char fmt_ulong(unsigned long v, char *buf) {
    char tmp[11];
    unsigned char n = 0, len = 0;
    do { tmp[n++] = (char)('0' + (unsigned char)(v % 10)); v /= 10; } while (v);
    while (n) buf[len++] = tmp[--n];
    buf[len] = 0;
    return len;
}

/* Left-aligned in a field of `w`, padded with spaces so a shrinking value can't
 * leave stale digits behind. */
static void put_money(unsigned char x, unsigned char y, Money v,
                      unsigned char w, unsigned char a) {
    char buf[20];
    unsigned char len = fmt_money(v, buf), i;
    put_str(x, y, buf, a);
    for (i = len; i < w; i++) put_cell(' ', a);
}
static void put_num(unsigned char x, unsigned char y, unsigned int v,
                    unsigned char w, unsigned char a) {
    char buf[8];
    unsigned char len = fmt_num(v, buf), i;
    put_str(x, y, buf, a);
    for (i = len; i < w; i++) put_cell(' ', a);
}

/* ============================================================================
 * World data
 *
 * Names are the prototype's, spellings included ("Grisley Gultch", "Virgina
 * City") -- they are the author's own and changing them is a call for him, not
 * a silent correction here.
 * ==========================================================================*/
static const char *const town_name[NTOWNS] = {
    "Grisley Gultch", "Fort Mesa", "Virgina City",
    "Calico Creek", "Thompson Springs", "Leadville Junction"
};

static const char *const good_name[NGOODS] = {
    "Feed", "Gold", "Whiskey", "Lumber", "Medicine", "Food", "Guns", "Water"
};
/* price = base + rnd(span). Straight from the prototype: the spread from $10
 * Feed to $30,000 Gold against a 100-unit wagon is what makes space the real
 * currency, so these numbers are kept exactly. Both fit an unsigned int. */
static const unsigned int price_base[NGOODS] = { 10, 15000, 300, 5000, 500, 30, 1000, 20 };
static const unsigned int price_span[NGOODS] = { 50, 15000, 500, 7500, 700, 210, 2500, 130 };

/* ============================================================================
 * The economy
 *
 * price[][] is the live price of every good in every town, all the time -- real
 * world state that persists, drifts and remembers what you did to it. Nothing is
 * regenerated on arrival.
 *
 * That distinction is the whole point. Rerolling on arrival meant the player's
 * own market impact was invisible: the reroll spanned a good's entire range while
 * the impact was a fraction of it, so cause and effect could not be perceived,
 * and the price did not even move until the next visit. Here, selling a hundred
 * units halves the price on screen, in front of you, and it climbs back over the
 * following days.
 * ==========================================================================*/
static unsigned int  price[NTOWNS][NGOODS];
static unsigned char need[NTOWNS];         /* short of this good, or SPEC_NONE */
static unsigned char excess[NTOWNS];       /* surplus of this good, or SPEC_NONE */

/* Your notebook: the last price you personally saw, and when. Prices drift, so
 * old notes are a lead rather than a fact. */
static unsigned int  seen_price[NTOWNS][NGOODS];
static unsigned int  seen_day[NTOWNS];     /* 0 = never been */
/* Specialities are remembered too, not read live. Otherwise the information
 * model contradicts itself: prices would go stale while a town's needs stayed
 * magically current from a hundred miles away. */
static unsigned char seen_need[NTOWNS];
static unsigned char seen_excess[NTOWNS];

/* A town's normal price for a good, derived from its current character. Computed
 * rather than stored so it stays correct when specialities shift. */
static unsigned int anchor_of(unsigned char t, unsigned char g) {
    unsigned int fifth = price_span[g] / ANCHOR_EXCESS;
    if (g == excess[t]) return price_base[g] + fifth;
    if (g == need[t])   return price_base[g] + price_span[g] - fifth;
    return price_base[g] + price_span[g] / 2;
}

/* Give a town a (possibly empty) character. */
static void spec_roll(unsigned char t) {
    need[t]   = (rndn(SPEC_PLAIN) == 0) ? SPEC_NONE : (unsigned char)rndn(NGOODS);
    excess[t] = (rndn(SPEC_PLAIN) == 0) ? SPEC_NONE : (unsigned char)rndn(NGOODS);
    if (excess[t] == need[t]) excess[t] = SPEC_NONE;   /* cannot be both */
}

static void market_init(void) {
    unsigned char t, g;
    for (t = 0; t < NTOWNS; t++) {
        spec_roll(t);
        seen_day[t] = 0;
        for (g = 0; g < NGOODS; g++) {
            unsigned int a = anchor_of(t, g);
            /* start scattered around normal so day one is not uniform */
            price[t][g] = a - a / 8 + rndn(a / 4 + 1);
            seen_price[t][g] = 0;
        }
    }
}

/* One day of trade everywhere: prices ease back toward normal and wander a
 * little. This is the "someone else is buying and selling too" that keeps a
 * market you have not touched from standing perfectly still. */
static void market_day(void) {
    unsigned char t, g;
    long a, p;

    for (t = 0; t < NTOWNS; t++) {
        if (rndn(100) < SPEC_SHIFT) spec_roll(t);   /* the map moves under you */

        for (g = 0; g < NGOODS; g++) {
            a = (long)anchor_of(t, g);
            p = (long)price[t][g];
            p += (a - p) / MEAN_REVERT;                     /* mean reversion */
            p += (long)rndn((unsigned int)(a / WALK_DIV) + 1)
               - (long)(a / (WALK_DIV * 2));                /* random walk */
            if (p < (long)(price_base[g] / 2 + 1)) p = (long)(price_base[g] / 2 + 1);
            if (p > (long)PRICE_CEILING) p = (long)PRICE_CEILING;
            price[t][g] = (unsigned int)p;
        }
    }
}

/* Your own trades, applied at once so the board changes under your hand.
 * MARKET_DEPTH units of volume moves a price by about half. */
static void market_sold(unsigned char t, unsigned char g, unsigned int qty) {
    long p = (long)price[t][g];
    p -= (p * (long)qty) / (long)MARKET_DEPTH;
    if (p < (long)(price_base[g] / 2 + 1)) p = (long)(price_base[g] / 2 + 1);
    price[t][g] = (unsigned int)p;
}
static void market_bought(unsigned char t, unsigned char g, unsigned int qty) {
    long p = (long)price[t][g];
    p += (p * (long)qty) / (long)MARKET_DEPTH;
    if (p > (long)PRICE_CEILING) p = (long)PRICE_CEILING;
    price[t][g] = (unsigned int)p;
}


/* ---- player / run state ---- */
static Money         cash    = START_CASH;
static Money         savings = 0;
static Money         debt    = START_DEBT;
static unsigned int  day     = 1;
static unsigned char town    = 0;
static unsigned int  max_space = START_SPACE;
static unsigned int  held[NGOODS];       /* units carried */
static unsigned char has_gun;            /* personal sidearm -- NOT the Guns cargo */
static unsigned char visited[NTOWNS];    /* seen its price board at least once */
static unsigned char running = 1;        /* cleared when the run ends */
static char          msg[SCR_W];         /* one-line feedback, shown under each screen */

static unsigned int space_used(void) {
    unsigned int i, n = 0;
    for (i = 0; i < NGOODS; i++) n += held[i];
    return n;
}
static unsigned int space_left(void) { return max_space - space_used(); }

/* Your sidearm, not your cargo. The Guns trade good is freight you buy and sell
 * like any other; this is the pistol on your hip, and the only thing it does is
 * let you choose to fight when you are held up. */
static unsigned char armed(void) { return has_gun; }

static void set_msg(const char *s) {
    unsigned char i = 0;
    while (s[i] && i < SCR_W - 1) { msg[i] = s[i]; i++; }
    msg[i] = 0;
}

/* Cargo counts toward the final tally at the prices where you finish. Forcing a
 * fire sale on the last day would just be a gotcha -- the interesting decision
 * is what to be holding, not remembering to liquidate. */
static Money cargo_value(void) {
    Money v = 0;
    unsigned char i;
    for (i = 0; i < NGOODS; i++) v += (Money)held[i] * (Money)price[town][i];
    return v;
}
static Money net_worth(void) { return cash + savings + cargo_value() - debt; }

/* Write down what this town is paying today. */
static void market_note(void) {
    unsigned char g;
    seen_day[town]    = day;
    seen_need[town]   = need[town];
    seen_excess[town] = excess[town];
    for (g = 0; g < NGOODS; g++) seen_price[town][g] = price[town][g];
}

/* ============================================================================
 * Numeric entry
 *
 * The common case by far is "all of it" -- everything I can afford, everything I
 * am carrying -- so that is what ENTER and A both fill in. They FILL rather than
 * commit, so the number is on screen before a second ENTER acts on it: no blind
 * "yes" to a figure you never saw. This is documented in FRONTIER.TXT, not on
 * screen; the game does not explain itself to the player.
 *
 * ESC cancels. That is safe here in a way it would not be in a game that reads
 * arrow keys (every arrow starts with ESC, so a bare one is ambiguous) -- this
 * program only ever reads plain keystrokes, so ESC is unambiguous.
 * ==========================================================================*/
#define ENTRY_W  12         /* width of the input field */
#define ENTRY_MAXDIGITS 10  /* a money field can hold seven figures and then some */

static long read_num(unsigned char x, unsigned char y, unsigned long maxv) {
    unsigned long v = 0;
    unsigned char len = 0, k;
    char buf[12];

    put_rep(x, y, ' ', ENTRY_W, A_TEXT);

    for (;;) {
        vshowcur((unsigned int)y * SCR_W + x + len);
        k = INCH();

        if (k == K_ESC) return -1L;

        /* A, or ENTER on an empty field, fills in the maximum. */
        if (k == 'A' || k == 'a' || (k == K_ENTER && len == 0)) {
            v = maxv;
            len = fmt_ulong(maxv, buf);
            put_str(x, y, buf, A_TEXT);
            put_rep(x + len, y, ' ', ENTRY_W - len, A_TEXT);
            continue;
        }
        if (k == K_ENTER) return (long)v;

        if (k == K_BACKSP || k == 127) {
            if (len) {
                v /= 10;
                len--;
                put_str(x + len, y, " ", A_TEXT);
            }
            continue;
        }
        if (k >= '0' && k <= '9' && len < ENTRY_MAXDIGITS) {
            unsigned long nv = v * 10 + (unsigned long)(k - '0');
            if (nv > maxv) continue;    /* refuse rather than silently clamping */
            v = nv;
            put_cell(k, A_TEXT);
            len++;
        }
    }
}

static unsigned char slen(const char *s) {
    unsigned char n = 0;
    while (s[n]) n++;
    return n;
}
/* Append to a message being assembled; returns the new length. */
static unsigned char app(char *dst, unsigned char n, const char *s) {
    while (*s) dst[n++] = *s++;
    return n;
}

/* Shared "how much?" prompt for the bank, loan office and wagon yard. Shows the
 * real ceiling so the player never types a number only to be refused, and
 * returns -1 when cancelled. */
static long ask_amount(const char *what, unsigned long maxv, unsigned char money) {
    char buf[24];
    unsigned char n, x = 4 + slen(what);

    clear_row(18);
    put_str(4, 18, what, A_TEXT);
    put_str(x + 1, 18, "(max", A_DIM);
    n = money ? fmt_money((Money)maxv, buf) : fmt_num((unsigned int)maxv, buf);
    put_str(x + 6, 18, buf, A_CASH);
    put_str(x + 6 + n, 18, "):", A_DIM);
    return read_num(x + 9 + n, 18, maxv);
}

/* Wait for acknowledgement, with the cursor parked out of the way. */
static void press_any(unsigned char y) {
    put_str(4, y, "Press any key.", A_DIM);
    vhidecur();
    INCH();
}

/* ============================================================================
 * Shared chrome
 * ==========================================================================*/
static void draw_frame(const char *title) {
    put_cell(G_TL, A_DIM);
    put_rep(1, 0, G_HBAR, SCR_W - 2, A_DIM);
    vaddr(SCR_W - 1); put_cell(G_TR, A_DIM);

    vaddr((unsigned int)1 * SCR_W); put_cell(G_VBAR, A_DIM);
    vaddr((unsigned int)1 * SCR_W + SCR_W - 1); put_cell(G_VBAR, A_DIM);
    put_str(2, 1, title, A_TITLE);
    put_str(30, 1, town_name[town], A_TITLE);
    put_str(62, 1, "DAY", A_DIM);
    put_num(66, 1, day, 3, A_TEXT);
    put_str(69, 1, "OF", A_DIM);
    put_num(72, 1, TOTAL_DAYS, 3, A_TEXT);

    put_rep(0, 2, G_HBAR, SCR_W, A_DIM);
}

static void draw_purse(unsigned char y) {
    put_str(2, y, "CASH", A_DIM);
    put_money(9, y, cash, 14, A_CASH);
    put_str(26, y, "DEBT", A_DIM);
    put_money(33, y, debt, 14, A_DEBT);
    put_str(52, y, "WAGON", A_DIM);
    put_num(60, y, space_used(), 4, A_TEXT);
    put_str(64, y, "/", A_DIM);
    put_num(65, y, max_space, 4, A_TEXT);
}

/* One good's row on a price board: number, name, speciality marker, price, held. */
static void draw_good(unsigned char i, unsigned char col, unsigned char row) {
    char lbl[4];
    lbl[0] = (char)('1' + i); lbl[1] = ')'; lbl[2] = 0;
    put_str(col, row, lbl, A_KEY);
    put_str(col + 3, row, good_name[i], A_GOOD);

    if (i == excess[town])    { put_str(col + 12, row, " ", A_DIM);
                                put_cell(G_CHEAP, A_CASH); }
    else if (i == need[town]) { put_str(col + 12, row, " ", A_DIM);
                                put_cell(G_DEAR, A_WARN); }
    else                        put_str(col + 12, row, "  ", A_DIM);

    put_money(col + 15, row, (Money)price[town][i], 9, A_CASH);
    put_str(col + 25, row, "x", A_DIM);
    put_num(col + 26, row, held[i], 4, held[i] ? A_TEXT : A_DIM);

    /* How today's price compares with what this town normally asks. The old
     * glut/tight labels are gone: the price itself is now the feedback, and this
     * just says whether it is a good one. */
    {
        unsigned int a = anchor_of(town, i);
        unsigned int p = price[town][i];
        if (p * 5 < a * 4)      put_str(col + 31, row, "low  ", A_CASH);
        else if (p * 4 > a * 5) put_str(col + 31, row, "high ", A_WARN);
        else                    put_str(col + 31, row, "     ", A_DIM);
    }
}

static void draw_board(unsigned char top) {
    unsigned char i;
    for (i = 0; i < NGOODS; i++)
        draw_good(i, (i < 4) ? 3 : 41, top + (i & 3));
}

/* ============================================================================
 * STATUS screen
 * ==========================================================================*/
static void status_screen(void) {
    cls();
    draw_frame("FRONTIER FORTUNE");

    draw_purse(4);
    put_str(2, 5, "SAVINGS", A_DIM);
    put_money(9, 5, savings, 14, A_CASH);
    put_str(52, 5, "ARMED", A_DIM);
    put_str(60, 5, armed() ? "yes" : "no ", armed() ? A_CASH : A_WARN);

    put_rep(0, 7, G_HBAR, SCR_W, A_DIM);
    put_str(2, 7, " PRICES ", A_DIM);
    put_cell(G_CHEAP, A_CASH); put_str(11, 7, " plentiful  ", A_DIM);
    put_cell(G_DEAR, A_WARN);  put_str(24, 7, " scarce ", A_DIM);
    draw_board(9);

    put_rep(0, 14, G_HBAR, SCR_W, A_DIM);
    put_str(4, 16, "S", A_KEY);  put_str(5, 16, "tore", A_TEXT);
    put_str(14, 16, "T", A_KEY); put_str(15, 16, "ravel", A_TEXT);
    put_str(25, 16, "B", A_KEY); put_str(26, 16, "ank", A_TEXT);
    put_str(34, 16, "D", A_KEY); put_str(35, 16, "ebt", A_TEXT);
    put_str(43, 16, "W", A_KEY); put_str(44, 16, "agons", A_TEXT);
    put_str(54, 16, "C", A_KEY); put_str(55, 16, "asino", A_TEXT);
    put_str(65, 16, "L", A_KEY); put_str(66, 16, "edger", A_TEXT);
    put_str(4, 17, "Q", A_KEY);  put_str(5, 17, "uit and settle up", A_TEXT);

    if (msg[0]) put_str(4, 19, msg, A_TITLE);
    put_str(4, 18, "Choose:", A_DIM);
}

/* ============================================================================
 * GENERAL STORE
 * ==========================================================================*/
static void store_buy(unsigned char i) {
    unsigned int  by_cash, by_space, maxq;
    long          q;
    Money         cost;
    char          line[SCR_W];
    unsigned char n;

    /* Purchases come out of cash only. Money in the bank has to be withdrawn
     * first -- that is what gives the Bank a purpose beyond safekeeping. */
    by_cash  = (unsigned int)(cash / (Money)price[town][i]);
    by_space = space_left();
    maxq     = (by_cash < by_space) ? by_cash : by_space;

    if (maxq == 0) {
        set_msg(by_space == 0 ? "No room in the wagon."
                              : "Not enough cash on hand.");
        return;
    }

    n = 0;
    n += fmt_num(maxq, line + n);
    line[n] = 0;
    clear_row(18);
    put_str(4, 18, "Buy how many ", A_TEXT);
    put_str(17, 18, good_name[i], A_GOOD);
    put_str(28, 18, "(max ", A_DIM);
    put_str(33, 18, line, A_TEXT);
    put_str(33 + n, 18, "):", A_DIM);

    q = read_num(37 + n, 18, maxq);
    if (q <= 0) { set_msg(""); return; }

    cost = (Money)q * (Money)price[town][i];      /* cast BEFORE multiplying: 16-bit
                                             * would overflow on big cargo */
    cash -= cost;
    held[i] += (unsigned int)q;
    market_bought(town, i, (unsigned int)q);   /* the board moves under your hand */
    market_note();

    n = 0;
    line[n++] = 'B'; line[n++] = 'o'; line[n++] = 'u'; line[n++] = 'g'; line[n++] = 'h';
    line[n++] = 't'; line[n++] = ' ';
    n += fmt_num((unsigned int)q, line + n);
    line[n++] = ' ';
    { const char *p = good_name[i]; while (*p) line[n++] = *p++; }
    line[n++] = ' '; line[n++] = 'f'; line[n++] = 'o'; line[n++] = 'r'; line[n++] = ' ';
    n += fmt_money(cost, line + n);
    line[n++] = '.'; line[n] = 0;
    set_msg(line);
}

static void store_sell(unsigned char i) {
    long          q;
    Money         gain;
    char          line[SCR_W];
    unsigned char n;

    if (held[i] == 0) { set_msg("You have none to sell."); return; }

    n = 0;
    n += fmt_num(held[i], line + n);
    line[n] = 0;
    clear_row(18);
    put_str(4, 18, "Sell how many ", A_TEXT);
    put_str(18, 18, good_name[i], A_GOOD);
    put_str(29, 18, "(have ", A_DIM);
    put_str(35, 18, line, A_TEXT);
    put_str(35 + n, 18, "):", A_DIM);

    q = read_num(39 + n, 18, held[i]);
    if (q <= 0) { set_msg(""); return; }

    gain = (Money)q * (Money)price[town][i];
    cash += gain;
    held[i] -= (unsigned int)q;
    market_sold(town, i, (unsigned int)q);     /* the board moves under your hand */
    market_note();

    n = 0;
    line[n++] = 'S'; line[n++] = 'o'; line[n++] = 'l'; line[n++] = 'd'; line[n++] = ' ';
    n += fmt_num((unsigned int)q, line + n);
    line[n++] = ' ';
    { const char *p = good_name[i]; while (*p) line[n++] = *p++; }
    line[n++] = ' '; line[n++] = 'f'; line[n++] = 'o'; line[n++] = 'r'; line[n++] = ' ';
    n += fmt_money(gain, line + n);
    line[n++] = '.'; line[n] = 0;
    set_msg(line);
}

/* The sidearm: a one-off purchase, paid in cash like any other. */
static void store_buy_gun(void) {
    if (has_gun)          { set_msg("You already carry a sidearm."); return; }
    if (cash < GUN_COST)  { set_msg("Not enough cash for a sidearm."); return; }
    cash -= GUN_COST;
    has_gun = 1;
    set_msg("You buy a sidearm. Road agents can be argued with now.");
}

static void store_screen(void) {
    unsigned char k, i;

    set_msg("");
    for (;;) {
        cls();
        draw_frame("GENERAL STORE");
        draw_purse(4);
        put_str(52, 5, "SPACE", A_DIM);
        put_num(60, 5, space_left(), 4, A_TEXT);

        put_rep(0, 7, G_HBAR, SCR_W, A_DIM);
        put_str(2, 7, " GOODS ", A_DIM);
        draw_board(9);

        put_rep(0, 14, G_HBAR, SCR_W, A_DIM);
        put_str(4, 15, "SIDEARM", A_DIM);
        if (has_gun) {
            put_str(13, 15, "carried", A_CASH);
        } else {
            put_str(13, 15, "for sale,", A_TEXT);
            put_money(23, 15, GUN_COST, 8, A_CASH);
            put_str(32, 15, "-- press", A_DIM);
            put_str(41, 15, "G", A_KEY);
            put_str(43, 15, "to buy one", A_TEXT);
        }

        put_str(4, 16, "1-8", A_KEY);
        put_str(8, 16, "pick a good", A_TEXT);
        put_str(24, 16, "ESC", A_KEY);
        put_str(28, 16, "back to town", A_TEXT);
        if (msg[0]) put_str(4, 20, msg, A_TITLE);

        clear_row(18);
        put_str(4, 18, "Which good?", A_DIM);
        vshowcur((unsigned int)18 * SCR_W + 16);
        k = INCH();
        if (k == K_ESC || k == 'Q' || k == 'q') break;
        if (k == 'G' || k == 'g') { store_buy_gun(); continue; }
        if (k < '1' || k > '8') continue;
        i = (unsigned char)(k - '1');

        clear_row(18);
        put_str(4, 18, good_name[i], A_GOOD);
        put_str(15, 18, "at", A_DIM);
        put_money(18, 18, (Money)price[town][i], 10, A_CASH);
        put_str(30, 18, "B", A_KEY); put_str(31, 18, "uy or ", A_TEXT);
        put_str(37, 18, "S", A_KEY); put_str(38, 18, "ell?", A_TEXT);
        vhidecur();
        k = INCH();
        if (k == 'B' || k == 'b')      store_buy(i);
        else if (k == 'S' || k == 's') store_sell(i);
    }
    set_msg("");
}

/* ============================================================================
 * What happens on the trail
 *
 * Up to three lines are collected during a journey and shown in one panel on
 * arrival, so an event and a holdup in the same trip read as one story rather
 * than two interruptions.
 * ==========================================================================*/
static char          trail[3][SCR_W];
static unsigned char trail_n;

static void trail_add(const char *s) {
    unsigned char i = 0;
    if (trail_n >= 3) return;
    while (s[i] && i < SCR_W - 1) { trail[trail_n][i] = s[i]; i++; }
    trail[trail_n][i] = 0;
    trail_n++;
}

static void trail_show(void) {
    unsigned char i;
    if (!trail_n) return;

    put_rep(6, 8, G_HBAR, 68, A_DIM);
    for (i = 0; i < 3; i++) put_rep(6, 9 + i, ' ', 68, A_TEXT);
    put_str(8, 8, " ON THE TRAIL ", A_TITLE);
    for (i = 0; i < trail_n; i++) put_str(8, 9 + i, trail[i], A_TEXT);
    put_rep(6, 12, G_HBAR, 68, A_DIM);
    press_any(14);
}

/* Scale a price without overflowing. Prices live in an unsigned int, and the
 * prototype's "all items in demand" event multiplied by four -- Gold at $29,999
 * would wrap. Doubling is the most the type allows across every good, so that is
 * the ceiling used here rather than clamping, which would quadruple Feed while
 * barely moving Gold and make the event feel arbitrary. */
static void price_scale(unsigned char i, unsigned char mul, unsigned char div) {
    unsigned long p = ((unsigned long)price[town][i] * mul) / div;
    if (p < 1UL) p = 1UL;
    /* Doubling fits every base price, but a price already lifted by market
     * pressure could not be doubled safely, hence the clamp as well. */
    if (p > 65000UL) p = 65000UL;
    price[town][i] = (unsigned int)p;
}

/* Take a bite out of the most valuable stack in the wagon. Road agents are not
 * stupid; they go for the Gold, not the Feed. Returns the good taken, or 255. */
static unsigned char rob_cargo(unsigned char divisor) {
    unsigned char i, best = 255;
    Money bestval = 0, v;
    unsigned int qty;

    for (i = 0; i < NGOODS; i++) {
        if (!held[i]) continue;
        v = (Money)held[i] * (Money)price[town][i];
        if (v > bestval) { bestval = v; best = i; }
    }
    if (best == 255) return 255;

    qty = held[best] / divisor;
    if (qty == 0) qty = 1;
    held[best] -= qty;
    return best;
}

/* A holdup. Savings are never touched -- that is precisely what the bank is for,
 * and it is the only thing that makes parking money worth losing liquidity. */
static void holdup_take(unsigned char heavy) {
    unsigned char good;
    char line[SCR_W];
    unsigned char n;
    Money take;

    /* They take whichever is worth more to them: the purse or the load. */
    if (cash > cargo_value()) {
        take = heavy ? cash / 2 : cash / 4;
        if (take <= 0) take = cash;
        cash -= take;
        n = app(line, 0, "They take ");
        n += fmt_money(take, line + n);
        n = app(line, n, " from your purse and ride off.");
        line[n] = 0;
        trail_add(line);
        return;
    }

    good = rob_cargo(heavy ? 2 : 4);
    if (good == 255) {
        trail_add("You have nothing worth taking. They ride off disgusted.");
        return;
    }
    n = app(line, 0, "They cut loose your ");
    n = app(line, n, good_name[good]);
    n = app(line, n, " and ride off.");
    line[n] = 0;
    trail_add(line);
}

static void road_agents(void) {
    unsigned char k;

    if (!armed()) {
        trail_add("Road agents block the trail. Unarmed, you can only watch.");
        holdup_take(0);
        return;
    }

    /* Armed, you get a say. The sidearm does not make you safer by existing --
     * it buys you this decision, and fighting is a genuine gamble. */
    trail_show();
    trail_n = 0;

    put_rep(6, 8, G_HBAR, 68, A_DIM);
    put_rep(6, 9, ' ', 68, A_TEXT);
    put_rep(6, 10, ' ', 68, A_TEXT);
    put_str(8, 8, " ROAD AGENTS ", A_WARN);
    put_str(8, 9, "Riders block the trail with guns drawn. Your hand is on your pistol.",
            A_TEXT);
    put_str(8, 10, "F", A_KEY); put_str(9, 10, "ight, or ", A_TEXT);
    put_str(18, 10, "H", A_KEY); put_str(19, 10, "and it over?", A_TEXT);
    put_rep(6, 11, G_HBAR, 68, A_DIM);
    vhidecur();

    for (;;) {
        k = INCH();
        if (k == 'H' || k == 'h') { holdup_take(0); return; }
        if (k == 'F' || k == 'f') break;
    }

    if (rndn(100) < FIGHT_WIN) {
        trail_add("You draw first. They scatter into the brush with nothing.");
    } else {
        trail_add("You are outdrawn. They take their time about it.");
        holdup_take(1);
        has_gun = 0;
        trail_add("Your pistol goes with them.");
    }
}

/* One random event, applied after arrival so price changes land on the new
 * town's board. Returns the change to the day cost: -1 shortcut, +1 delay. */
static signed char random_event(void) {
    unsigned char e = (unsigned char)rndn(7);
    unsigned char i, g;
    unsigned int  qty;
    unsigned long budget;
    char line[SCR_W];
    unsigned char n;

    switch (e) {
        case 0:
            trail_add("A hidden shortcut! The journey costs you no time at all.");
            return -1;
        case 1:
            trail_add("The bridge is out and the detour is long. Travel took two days.");
            return 1;

        case 2:   /* a cache by the roadside */
            g = (unsigned char)rndn(NGOODS);
            /* Valued, not counted: the prototype filled the entire wagon, which
             * with Gold was an instant win. A cache is worth a fixed sum, so
             * finding cheap goods means a lot of them and Gold means one bar. */
            budget = FIND_MIN + rndn(FIND_SPAN);
            qty = (unsigned int)(budget / price[town][g]);
            if (qty == 0) qty = 1;
            if (qty > FIND_MAX_QTY) qty = FIND_MAX_QTY;
            if (qty > space_left()) qty = space_left();
            if (qty == 0) {
                trail_add("You pass a cache of goods by the road, but the wagon is full.");
                return 0;
            }
            held[g] += qty;
            n = app(line, 0, "A cache by the road! You load ");
            n += fmt_num(qty, line + n);
            line[n++] = ' ';
            n = app(line, n, good_name[g]);
            line[n++] = '.'; line[n] = 0;
            trail_add(line);
            return 0;

        case 3:   /* lost cargo -- the prototype declared this and left it empty */
            g = rob_cargo(3);
            if (g == 255) {
                trail_add("A wheel splits on the rocks. Nothing aboard to lose.");
                return 0;
            }
            n = app(line, 0, "You ford a swollen river and lose some of your ");
            n = app(line, n, good_name[g]);
            line[n++] = '.'; line[n] = 0;
            trail_add(line);
            return 0;

        case 4:
            g = (unsigned char)rndn(NGOODS);
            price_scale(g, 2, 1);
            n = app(line, 0, "Word of a shortage of ");
            n = app(line, n, good_name[g]);
            n = app(line, n, " -- the price here has doubled.");
            line[n] = 0;
            trail_add(line);
            return 0;

        case 5:
            g = (unsigned char)rndn(NGOODS);
            price_scale(g, 1, 2);
            n = app(line, 0, "The market here is flooded with ");
            n = app(line, n, good_name[g]);
            n = app(line, n, " -- the price has crashed.");
            line[n] = 0;
            trail_add(line);
            return 0;

        default:
            for (i = 0; i < NGOODS; i++) price_scale(i, 2, 1);
            trail_add("A new settlement has formed. Everything is in demand here!");
            return 0;
    }
}

/* ============================================================================
 * TRAVEL, the clock, and the end of the run
 * ==========================================================================*/

/* Every day that passes compounds the debt. The prototype never charged any
 * interest at all, which left the 60 days with nothing to push against; this is
 * the single change that makes the clock matter. Integer division means a debt
 * under DEBT_DIVISOR accrues nothing, which is a fine place for it to stop. */
static void advance_days(unsigned char days) {
    while (days--) {
        day++;
        debt += debt / DEBT_DIVISOR;
        market_day();
    }
}

/* ============================================================================
 * High-score table
 *
 * Stored beside the game as FRONTIER.SCO. Every read is treated as untrusted:
 * a short, truncated or future-version file yields an empty table rather than
 * garbage entries, because losing the table is a small annoyance and showing
 * nonsense is a bug.
 * ==========================================================================*/
static char          hs_name[NSCORES][NAMELEN + 1];
static Money         hs_score[NSCORES];
static unsigned int  hs_days[NSCORES];
static unsigned char hs_n;

static void hs_clear(void) {
    unsigned char i;
    hs_n = 0;
    for (i = 0; i < NSCORES; i++) { hs_name[i][0] = 0; hs_score[i] = 0; hs_days[i] = 0; }
}

static void put_long(Money v) {
    unsigned long u = (unsigned long)v;
    dputb((unsigned char)(u & 0xFF));
    dputb((unsigned char)((u >> 8) & 0xFF));
    dputb((unsigned char)((u >> 16) & 0xFF));
    dputb((unsigned char)((u >> 24) & 0xFF));
}

/* Reads four bytes little-endian. Sets *ok to 0 if the file ran out. */
static Money get_long(unsigned char *ok) {
    unsigned long u = 0;
    unsigned char s;
    int b;
    for (s = 0; s < 32; s += 8) {
        b = dgetb();
        if (b < 0) { *ok = 0; return 0; }
        u |= ((unsigned long)(unsigned char)b) << s;
    }
    return (Money)u;
}

static void hs_load(void) {
    unsigned char ok = 1, i, j, count;
    int b;

    hs_clear();
    if (dopen_read(SCORE_FILE) != 0) return;      /* no table yet is normal */

    b = dgetb(); if (b != SCORE_MAGIC) { dclose(); return; }
    b = dgetb(); if (b != SCORE_VER)   { dclose(); return; }
    b = dgetb(); if (b < 0 || b > NSCORES) { dclose(); return; }
    count = (unsigned char)b;

    for (i = 0; i < count; i++) {
        for (j = 0; j < NAMELEN; j++) {
            b = dgetb();
            if (b < 0) { ok = 0; break; }
            hs_name[i][j] = (char)b;
        }
        hs_name[i][NAMELEN] = 0;
        if (!ok) break;
        hs_score[i] = get_long(&ok);
        if (!ok) break;
        b = dgetb();
        if (b < 0) { ok = 0; break; }
        hs_days[i] = (unsigned char)b;
    }
    dclose();

    /* A truncated file keeps whatever whole entries were read. */
    hs_n = ok ? count : i;
}

static void hs_save(void) {
    unsigned char i, j;

    if (dopen_write(SCORE_FILE) != 0) return;     /* read-only disk: just skip */
    dputb(SCORE_MAGIC);
    dputb(SCORE_VER);
    dputb(hs_n);
    for (i = 0; i < hs_n; i++) {
        for (j = 0; j < NAMELEN; j++)
            dputb((unsigned char)(hs_name[i][j] ? hs_name[i][j] : ' '));
        put_long(hs_score[i]);
        dputb((unsigned char)hs_days[i]);
    }
    dclose();
}

/* Where this score would land, or NSCORES if it does not place. */
static unsigned char hs_rank(Money score) {
    unsigned char i;
    for (i = 0; i < hs_n; i++)
        if (score > hs_score[i]) return i;
    return (hs_n < NSCORES) ? hs_n : NSCORES;
}

static void hs_insert(unsigned char at, const char *name, Money score,
                      unsigned int days) {
    unsigned char i, j;

    if (hs_n < NSCORES) hs_n++;
    for (i = hs_n - 1; i > at; i--) {           /* shift the losers down */
        for (j = 0; j <= NAMELEN; j++) hs_name[i][j] = hs_name[i - 1][j];
        hs_score[i] = hs_score[i - 1];
        hs_days[i]  = hs_days[i - 1];
    }
    for (j = 0; j < NAMELEN && name[j]; j++) hs_name[at][j] = name[j];
    hs_name[at][j] = 0;
    hs_score[at] = score;
    hs_days[at]  = days;
}

/* Type a name for the table. Printables only, so a stray control byte can't end
 * up written into the file. */
static void read_name(unsigned char x, unsigned char y, char *out) {
    unsigned char len = 0, k;

    put_rep(x, y, ' ', NAMELEN + 1, A_TEXT);
    for (;;) {
        vshowcur((unsigned int)y * SCR_W + x + len);
        k = INCH();
        if (k == K_ENTER) break;
        if ((k == K_BACKSP || k == 127) && len) {
            len--;
            put_str(x + len, y, " ", A_TEXT);
            continue;
        }
        if (k >= ' ' && k < 127 && len < NAMELEN) {
            out[len++] = (char)k;
            put_cell(k, A_TEXT);
        }
    }
    out[len] = 0;
    if (len == 0) { out[0] = 'T'; out[1] = 'R'; out[2] = 'A'; out[3] = 'D';
                    out[4] = 'E'; out[5] = 'R'; out[6] = 0; }
}

static void hs_screen(unsigned char highlight) {
    unsigned char i;
    char buf[8];

    cls();
    draw_frame("BOOK OF FORTUNES");
    put_str(6, 4, "#", A_DIM);
    put_str(9, 4, "TRADER", A_DIM);
    put_str(26, 4, "NET WORTH", A_DIM);
    put_str(48, 4, "DAYS", A_DIM);
    put_rep(6, 5, G_HBAR, 60, A_DIM);

    if (hs_n == 0) {
        put_str(9, 7, "No names yet. Yours could be the first.", A_DIM);
    }
    for (i = 0; i < hs_n; i++) {
        unsigned char a = (i == highlight) ? A_TITLE : A_TEXT;
        fmt_num(i + 1, buf);
        put_str(6, 6 + i, buf, A_DIM);
        put_str(9, 6 + i, hs_name[i], a);
        put_money(26, 6 + i, hs_score[i], 18, hs_score[i] >= 0 ? A_CASH : A_DEBT);
        put_num(48, 6 + i, hs_days[i], 4, a);
    }
    press_any(17);
}

static void end_screen(const char *why) {
    Money cargo = cargo_value();
    Money net   = net_worth();
    unsigned int days = (day - 1 > TOTAL_DAYS) ? TOTAL_DAYS : day - 1;
    unsigned char rank;
    char name[NAMELEN + 1];

    cls();
    draw_frame("THE RECKONING");
    put_str(4, 5, why, A_TITLE);

    put_str(6, 8,  "Cash", A_DIM);          put_money(22, 8,  cash,    16, A_CASH);
    put_str(6, 9,  "Savings", A_DIM);       put_money(22, 9,  savings, 16, A_CASH);
    put_str(6, 10, "Cargo at hand", A_DIM); put_money(22, 10, cargo,   16, A_CASH);
    put_str(6, 11, "Debt", A_DIM);          put_money(22, 11, -debt,   16, A_DEBT);
    put_rep(6, 12, G_HBAR, 32, A_DIM);
    put_str(6, 13, "NET WORTH", A_TITLE);   put_money(22, 13, net, 16,
                                                      net >= 0 ? A_CASH : A_DEBT);

    put_str(6, 15, "Days on the trail", A_DIM);
    put_num(26, 15, days, 4, A_TEXT);

    if (net >= 1000000L)     put_str(4, 17, "A fortune. They will name the street after you.", A_TITLE);
    else if (net >= 100000L) put_str(4, 17, "A handsome pile. You leave the frontier rich.", A_TITLE);
    else if (net > 0)        put_str(4, 17, "You settle up and walk away with something.", A_TEXT);
    else                     put_str(4, 17, "Broke. The loan shark will be looking for you.", A_WARN);

    press_any(19);

    hs_load();
    rank = hs_rank(net);
    if (rank < NSCORES) {
        clear_row(19); clear_row(21);
        put_str(4, 19, "You have made the book. Your name?", A_TITLE);
        read_name(4, 21, name);
        hs_insert(rank, name, net, days);
        hs_save();
    } else {
        rank = NSCORES;             /* nothing to highlight */
    }
    hs_screen(rank);
    running = 0;
}

/* Make the journey. Returns 0 if the run ended on arrival.
 *
 * Order matters here. The event is chosen first because it can change the day
 * cost. Its price effects land on the live board directly -- there is no reroll
 * to wipe them out any more. Road agents come last, when they can see what you
 * are actually carrying. */
static unsigned char arrive(unsigned char dest) {
    signed char   adjust = 0;
    unsigned char days;

    trail_n = 0;

    town = dest;
    visited[dest] = 1;

    if (rndn(100) < EVENT_CHANCE) adjust = random_event();
    if (rndn(100) < HOLDUP_CHANCE) road_agents();

    days = (unsigned char)(1 + adjust);     /* -1 shortcut, +1 delay */
    advance_days(days);
    market_note();                          /* write down today's board here */

    trail_show();

    if (day > TOTAL_DAYS) {
        end_screen("Your sixty days are up.");
        return 0;
    }
    return 1;
}

static void travel_screen(void) {
    unsigned char k, t, row;
    char line[SCR_W];
    unsigned char n;

    cls();
    draw_frame("TRAVEL");
    draw_purse(4);

    put_rep(0, 6, G_HBAR, SCR_W, A_DIM);
    put_str(2, 6, " WHERE TO? ", A_DIM);
    put_str(30, 6, " one day on the trail ", A_DIM);

    for (t = 0; t < NTOWNS; t++) {
        row = 7 + t;
        line[0] = (char)('1' + t); line[1] = ')'; line[2] = 0;
        put_str(6, row, line, t == town ? A_DIM : A_KEY);
        put_str(10, row, town_name[t], t == town ? A_DIM : A_TEXT);

        if (t == town) {
            put_str(32, row, "(you are here)", A_DIM);
        } else if (seen_day[t]) {
            /* What you knew when you left. Specialities shift and prices drift,
             * so this is a lead, not a fact -- hence the age.
             *
             * The names go through locals rather than a ternary: cc65 will not
             * unify a string literal with a `const char *const` array element. */
            {
                const char *nn = "--", *xn = "--";
                if (seen_need[t]   != SPEC_NONE) nn = good_name[seen_need[t]];
                if (seen_excess[t] != SPEC_NONE) xn = good_name[seen_excess[t]];
                put_str(30, row, "needs:", A_DIM);
                put_str(37, row, nn, A_WARN);
                put_str(48, row, "spare:", A_DIM);
                put_str(55, row, xn, A_CASH);
            }
            put_num(67, row, day - seen_day[t], 3, A_DIM);
            put_str(70, row, "d ago", A_DIM);
        } else {
            put_str(30, row, "you have not been there", A_DIM);
        }
    }

    put_rep(0, 15, G_HBAR, SCR_W, A_DIM);
    put_str(4, 16, "1-6", A_KEY);
    put_str(8, 16, "set out", A_TEXT);
    put_str(24, 16, "ESC", A_KEY);
    put_str(28, 16, "stay put", A_TEXT);

    n = 0;
    { const char *p = "Interest owed each day on the trail: "; while (*p) line[n++] = *p++; }
    n += fmt_money(debt / DEBT_DIVISOR, line + n);
    line[n] = 0;
    put_str(4, 18, line, A_DIM);

    clear_row(20);
    put_str(4, 20, "Destination:", A_DIM);
    vshowcur((unsigned int)20 * SCR_W + 17);
    k = INCH();

    if (k == K_ESC || k == 'Q' || k == 'q') return;
    if (k < '1' || k > '6') return;
    t = (unsigned char)(k - '1');
    if (t == town) { set_msg("You are already there."); return; }

    if (!arrive(t)) return;             /* run may have ended on arrival */

    n = 0;
    { const char *p = "You reach "; while (*p) line[n++] = *p++; }
    { const char *p = town_name[town]; while (*p) line[n++] = *p++; }
    line[n++] = '.'; line[n++] = ' ';
    { const char *p = "Prices have moved."; while (*p) line[n++] = *p++; }
    line[n] = 0;
    set_msg(line);
}

/* ============================================================================
 * BANK
 *
 * Savings earn nothing. The bank exists so cash can be put somewhere road
 * agents cannot reach it -- and since every purchase is made in cash, parking
 * money is a real trade against being ready to buy. It is a safe, not a fund.
 * ==========================================================================*/
static void bank_screen(void) {
    unsigned char k, n;
    long amt;
    char line[SCR_W];

    set_msg("");
    for (;;) {
        cls();
        draw_frame("BANK");

        put_str(6, 5, "Cash on hand", A_DIM);
        put_money(24, 5, cash, 16, A_CASH);
        put_str(6, 6, "In the bank", A_DIM);
        put_money(24, 6, savings, 16, A_CASH);

        put_rep(0, 12, G_HBAR, SCR_W, A_DIM);

        put_str(4, 14, "D", A_KEY); put_str(5, 14, "eposit", A_TEXT);
        put_str(16, 14, "W", A_KEY); put_str(17, 14, "ithdraw", A_TEXT);
        put_str(30, 14, "ESC", A_KEY); put_str(34, 14, "back to town", A_TEXT);
        if (msg[0]) put_str(4, 20, msg, A_TITLE);

        clear_row(18);
        put_str(4, 18, "Choose:", A_DIM);
        vshowcur((unsigned int)18 * SCR_W + 12);
        k = INCH();
        if (k == K_ESC || k == 'Q' || k == 'q') break;

        if (k == 'D' || k == 'd') {
            if (cash <= 0) { set_msg("You have no cash to deposit."); continue; }
            amt = ask_amount("Deposit how much", (unsigned long)cash, 1);
            if (amt <= 0) { set_msg(""); continue; }
            cash -= (Money)amt; savings += (Money)amt;
            n = app(line, 0, "Deposited ");
            n += fmt_money((Money)amt, line + n);
            n = app(line, n, "."); line[n] = 0;
            set_msg(line);
        } else if (k == 'W' || k == 'w') {
            if (savings <= 0) { set_msg("Your account is empty."); continue; }
            amt = ask_amount("Withdraw how much", (unsigned long)savings, 1);
            if (amt <= 0) { set_msg(""); continue; }
            savings -= (Money)amt; cash += (Money)amt;
            n = app(line, 0, "Withdrew ");
            n += fmt_money((Money)amt, line + n);
            n = app(line, n, "."); line[n] = 0;
            set_msg(line);
        }
    }
    set_msg("");
}

/* ============================================================================
 * LOAN OFFICE
 *
 * Borrowing was declared in the prototype (`borrowMoney:`) and never written.
 * It belongs: capital now against compounding later is the central bet of the
 * genre, and it gives an early player a way to buy into the expensive goods.
 * ==========================================================================*/
static void debt_screen(void) {
    unsigned char k, n;
    long amt;
    Money room, payable;
    char line[SCR_W];

    set_msg("");
    for (;;) {
        cls();
        draw_frame("LOAN OFFICE");

        put_str(6, 5, "Cash on hand", A_DIM);
        put_money(24, 5, cash, 16, A_CASH);
        put_str(6, 6, "You owe", A_DIM);
        put_money(24, 6, debt, 16, A_DEBT);
        put_str(6, 7, "Interest per day", A_DIM);
        put_money(24, 7, debt / DEBT_DIVISOR, 16, A_DEBT);

        put_rep(0, 12, G_HBAR, SCR_W, A_DIM);

        put_str(4, 14, "P", A_KEY); put_str(5, 14, "ay", A_TEXT);
        put_str(16, 14, "B", A_KEY); put_str(17, 14, "orrow", A_TEXT);
        put_str(30, 14, "ESC", A_KEY); put_str(34, 14, "back to town", A_TEXT);
        if (msg[0]) put_str(4, 20, msg, A_TITLE);

        clear_row(18);
        put_str(4, 18, "Choose:", A_DIM);
        vshowcur((unsigned int)18 * SCR_W + 12);
        k = INCH();
        if (k == K_ESC || k == 'Q' || k == 'q') break;

        if (k == 'P' || k == 'p') {
            payable = (cash < debt) ? cash : debt;   /* never overpay the debt */
            if (payable <= 0) {
                set_msg(debt <= 0 ? "You owe nothing. Enjoy it."
                                  : "No cash on hand to pay with.");
                continue;
            }
            amt = ask_amount("Pay how much", (unsigned long)payable, 1);
            if (amt <= 0) { set_msg(""); continue; }
            cash -= (Money)amt; debt -= (Money)amt;
            n = app(line, 0, "Paid ");
            n += fmt_money((Money)amt, line + n);
            n = app(line, n, debt == 0 ? ". You are square with the shark."
                                       : " off the debt.");
            line[n] = 0;
            set_msg(line);
        } else if (k == 'B' || k == 'b') {
            room = MAX_DEBT - debt;
            if (room <= 0) { set_msg("They will not lend you another cent."); continue; }
            amt = ask_amount("Borrow how much", (unsigned long)room, 1);
            if (amt <= 0) { set_msg(""); continue; }
            cash += (Money)amt; debt += (Money)amt;
            n = app(line, 0, "Borrowed ");
            n += fmt_money((Money)amt, line + n);
            n = app(line, n, ". It starts earning against you tomorrow.");
            line[n] = 0;
            set_msg(line);
        }
    }
    set_msg("");
}

/* ============================================================================
 * WAGON YARD
 * ==========================================================================*/
static void wagons_screen(void) {
    unsigned char k, n;
    long qty;
    unsigned int maxq;
    Money cost;
    char line[SCR_W];

    set_msg("");
    for (;;) {
        cls();
        draw_frame("WAGON YARD");

        put_str(6, 5, "Cash on hand", A_DIM);
        put_money(24, 5, cash, 16, A_CASH);
        put_str(6, 6, "Wagon space", A_DIM);
        put_num(24, 6, space_used(), 5, A_TEXT);
        put_str(29, 6, "/", A_DIM);
        put_num(30, 6, max_space, 5, A_TEXT);

        put_rep(0, 8, G_HBAR, SCR_W, A_DIM);
        put_str(4, 10, "Another wagon carries", A_DIM);
        put_num(26, 10, WAGON_SPACE, 4, A_TEXT);
        put_str(30, 10, "more units, and costs", A_DIM);
        put_money(52, 10, WAGON_COST, 10, A_CASH);

        put_rep(0, 12, G_HBAR, SCR_W, A_DIM);
        put_str(4, 14, "B", A_KEY); put_str(5, 14, "uy wagons", A_TEXT);
        put_str(30, 14, "ESC", A_KEY); put_str(34, 14, "back to town", A_TEXT);
        if (msg[0]) put_str(4, 20, msg, A_TITLE);

        clear_row(18);
        put_str(4, 18, "Choose:", A_DIM);
        vshowcur((unsigned int)18 * SCR_W + 12);
        k = INCH();
        if (k == K_ESC || k == 'Q' || k == 'q') break;

        if (k == 'B' || k == 'b') {
            maxq = (unsigned int)(cash / WAGON_COST);
            if (maxq == 0) { set_msg("Not enough cash for a wagon."); continue; }
            qty = ask_amount("How many wagons", maxq, 0);
            if (qty <= 0) { set_msg(""); continue; }
            cost = (Money)qty * WAGON_COST;
            cash -= cost;
            max_space += (unsigned int)qty * WAGON_SPACE;
            n = app(line, 0, "Bought ");
            n += fmt_num((unsigned int)qty, line + n);
            n = app(line, n, qty == 1 ? " wagon for " : " wagons for ");
            n += fmt_money(cost, line + n);
            n = app(line, n, "."); line[n] = 0;
            set_msg(line);
        }
    }
    set_msg("");
}

/* ============================================================================
 * LEDGER -- what you wrote down, and how old it is
 *
 * The one screen where you can compare towns. It shows the price you personally
 * saw, not the live one, so planning a route means betting that a note is still
 * good. Prices drift daily and specialities shift, so the older the entry the
 * less it is worth -- which is the whole reason to keep travelling.
 * ==========================================================================*/
static const char *const good_abbr[NGOODS] = {
    "Feed", "Gold", "Wsky", "Lmbr", "Medi", "Food", "Guns", "Watr"
};

static void ledger_screen(void) {
    unsigned char t, g, row;
    char buf[8];

    cls();
    draw_frame("LEDGER");

    for (g = 0; g < NGOODS; g++)
        put_str(20 + g * 7, 5, good_abbr[g], A_TITLE);
    put_str(74, 5, "age", A_DIM);
    put_rep(2, 6, G_HBAR, 76, A_DIM);

    for (t = 0; t < NTOWNS; t++) {
        row = 8 + t;
        put_str(2, row, town_name[t], t == town ? A_TITLE : A_TEXT);

        if (!seen_day[t]) {
            put_str(20, row, "-- you have not been to this town --", A_DIM);
            continue;
        }
        for (g = 0; g < NGOODS; g++) {
            /* No thousands separators here: eight columns have to fit in 80. */
            fmt_num(seen_price[t][g], buf);
            put_str(20 + g * 7, row, buf, A_CASH);
            put_str(20 + g * 7 + slen(buf), row, "      ", A_DIM);
        }
        put_num(74, row, day - seen_day[t], 3, A_DIM);
    }

    put_rep(2, 13, G_HBAR, 76, A_DIM);
    press_any(15);
}

/* ============================================================================
 * CASINO -- Hi-Lo
 *
 * Straight from the prototype: two cards, you call whether yours beats the
 * dealer's, ties push. With both cards drawn uniformly that is an even-money
 * bet -- 6/13 to win, 6/13 to lose, 1/13 to push -- so the tables are not an
 * income stream. They are a variance tool for a player who is behind and needs
 * a swing, which is exactly what a desperate trader would use them for.
 *
 * Bets are cash-only, like every other purchase in the game. You cannot gamble
 * away money that is in the bank without walking to the bank first, which is a
 * small and rather fitting piece of protection from yourself.
 * ==========================================================================*/
static const char *const card_name[13] = {
    "Two", "Three", "Four", "Five", "Six", "Seven", "Eight",
    "Nine", "Ten", "Jack", "Queen", "King", "Ace"
};

static void draw_card(unsigned char x, unsigned char y, const char *rank,
                      unsigned char a) {
    unsigned char i, w = 9;

    put_cell(G_TL, A_DIM);
    put_rep(x + 1, y, G_HBAR, w - 2, A_DIM);
    vaddr((unsigned int)y * SCR_W + x + w - 1); put_cell(G_TR, A_DIM);

    for (i = 1; i <= 3; i++) {
        vaddr((unsigned int)(y + i) * SCR_W + x);
        last_attr = 0xFF;
        put_cell(G_VBAR, A_DIM);
        put_rep(x + 1, y + i, ' ', w - 2, A_TEXT);
        vaddr((unsigned int)(y + i) * SCR_W + x + w - 1); put_cell(G_VBAR, A_DIM);
    }

    vaddr((unsigned int)(y + 4) * SCR_W + x); last_attr = 0xFF;
    put_cell(G_BL, A_DIM);
    put_rep(x + 1, y + 4, G_HBAR, w - 2, A_DIM);
    vaddr((unsigned int)(y + 4) * SCR_W + x + w - 1); put_cell(G_BR, A_DIM);

    /* centre the rank in the face */
    put_str(x + 1 + (w - 2 - slen(rank)) / 2, y + 2, rank, a);
}

static void casino_screen(void) {
    unsigned char k, dealer, player, high, n;
    long bet;
    char line[SCR_W];

    set_msg("");
    for (;;) {
        cls();
        draw_frame("CASINO");

        put_str(6, 4, "Cash on hand", A_DIM);
        put_money(24, 4, cash, 16, A_CASH);

        put_str(14, 7, "DEALER", A_DIM);
        put_str(48, 7, "YOU", A_DIM);
        draw_card(12, 8, "?", A_DIM);
        draw_card(46, 8, "?", A_DIM);

        put_str(4, 14, "B", A_KEY); put_str(5, 14, "et", A_TEXT);
        put_str(30, 14, "ESC", A_KEY); put_str(34, 14, "back to town", A_TEXT);
        if (msg[0]) put_str(4, 20, msg, A_TITLE);

        clear_row(18);
        put_str(4, 18, "Choose:", A_DIM);
        vshowcur((unsigned int)18 * SCR_W + 12);
        k = INCH();
        if (k == K_ESC || k == 'Q' || k == 'q') break;
        if (k != 'B' && k != 'b') continue;

        if (cash <= 0) { set_msg("You have no cash to bet."); continue; }
        bet = ask_amount("Bet how much", (unsigned long)cash, 1);
        if (bet <= 0) { set_msg(""); continue; }

        clear_row(18);
        put_str(4, 18, "Call it -- ", A_TEXT);
        put_str(15, 18, "H", A_KEY); put_str(16, 18, "igh or ", A_TEXT);
        put_str(23, 18, "L", A_KEY); put_str(24, 18, "ow?", A_TEXT);
        vhidecur();
        for (;;) {
            k = INCH();
            if (k == 'H' || k == 'h') { high = 1; break; }
            if (k == 'L' || k == 'l') { high = 0; break; }
            if (k == K_ESC)           { high = 2; break; }
        }
        if (high == 2) { set_msg("You think better of it."); continue; }

        dealer = (unsigned char)rndn(13);
        player = (unsigned char)rndn(13);

        /* Deal the dealer, then make the player ask for their own card. The
         * prototype wanted a dramatic pause here and left a bare comment where
         * the wait should be; a keypress is a better beat than a sleep anyway,
         * and it costs a turn-based game nothing. */
        draw_card(12, 8, card_name[dealer], A_TITLE);
        clear_row(18);
        put_str(4, 18, "Press a key to turn your card.", A_DIM);
        INCH();
        draw_card(46, 8, card_name[player], A_TITLE);

        n = 0;
        if (player == dealer) {
            n = app(line, 0, "A push. Your ");
            n += fmt_money((Money)bet, line + n);
            n = app(line, n, " stays where it is.");
        } else if ((high && player > dealer) || (!high && player < dealer)) {
            cash += (Money)bet;
            n = app(line, 0, "You called it. You win ");
            n += fmt_money((Money)bet, line + n);
            n = app(line, n, ".");
        } else {
            cash -= (Money)bet;
            n = app(line, 0, "Wrong call. The house takes ");
            n += fmt_money((Money)bet, line + n);
            n = app(line, n, ".");
        }
        line[n] = 0;

        clear_row(18);
        put_str(4, 18, line, A_TITLE);
        press_any(20);
        set_msg("");
    }
    set_msg("");
}

void main(void) {
    unsigned char k;

    rngv = rng_seed();
    if (rngv == 0) rngv = 0xACE1;       /* xorshift must never start at zero */

    vhidecur();
    market_init();
    visited[0] = 1;                     /* you start knowing your home town */
    market_note();
    set_msg("");

    while (running) {
        status_screen();
        vshowcur((unsigned int)18 * SCR_W + 12);
        k = INCH();

        if (k == 'Q' || k == 'q') {
            /* Quitting IS retiring -- you settle up and get your tally, rather
             * than the run silently evaporating. */
            clear_row(18);
            put_str(4, 18, "Retire now and settle up? (Y/N)", A_TITLE);
            vhidecur();
            k = INCH();
            if (k == 'Y' || k == 'y') { end_screen("You hang up your hat early."); }
            continue;
        }
        switch (k) {
            case 'S': case 's': store_screen(); break;
            case 'T': case 't': travel_screen(); break;
            case 'B': case 'b': bank_screen();   break;
            case 'D': case 'd': debt_screen();   break;
            case 'W': case 'w': wagons_screen(); break;
            case 'C': case 'c': casino_screen(); break;
            case 'L': case 'l': ledger_screen(); break;
            default:
                break;
        }
    }

    QUITDOS();
}
