/* ============================================================================
 * kpanic.c -- KERNEL PANIC: engine scaffold (build step 1).
 *
 * What works here: the fixed-tick simulation loop paced off the kernel's 60 Hz
 * jiffy counter, the bounded scroll region with a pinned HUD, the chip-side
 * scroll flowing the conduit downward, and non-blocking input.
 *
 * Deliberately NOT here yet (see DESIGN.md build path): procedural meandering
 * terrain (step 2), energy/OVERCLOCK and firing (step 3), enemies (step 4),
 * power-ups (5), bosses (6), juice (7). The conduit walls are fixed columns and
 * the craft has no collision or weapon -- this build exists to prove the loop
 * paces correctly and the HUD stays pinned while the world scrolls.
 * ==========================================================================*/
#include "kpanic.h"

/* ---- RNG: inline xorshift, seeded from the RTC (cheaper per call than the
 * K_GET_RAND_NUM ABI, which we'd otherwise hit once per object per frame) ---- */
static unsigned int rngv = 0xACE1;
unsigned int rnd16(void) {
    rngv ^= (unsigned int)(rngv << 7);
    rngv ^= (unsigned int)(rngv >> 9);
    rngv ^= (unsigned int)(rngv << 8);
    return rngv;
}
unsigned char rndn(unsigned char n) { return (unsigned char)(rnd16() % n); }

/* int -> ASCII digits in reverse order; returns the digit count. */
signed char utoa(unsigned int v, char *buf) {
    signed char i = 0;
    do { buf[i++] = (char)('0' + (v % 10)); v /= 10; } while (v);
    return i;
}

/* ---- screen primitives ---- */
static unsigned char last_attr;         /* skip redundant vattr() within a run */

static void put_cell(unsigned char g, unsigned char a) {
    if (a != last_attr) { vattr(a); last_attr = a; }
    vputc(g);
}
static void put_str(unsigned char x, unsigned char y, const char *s, unsigned char a) {
    vaddr((unsigned int)y * SCR_W + x);
    last_attr = 0xFF;
    while (*s) put_cell((unsigned char)*s++, a);
}
/* Right-pad to `w` cells so a shrinking number can't leave stale digits behind. */
static void put_num(unsigned char x, unsigned char y, unsigned int v,
                    unsigned char w, unsigned char a) {
    char buf[6];
    signed char n = utoa(v, buf), j;
    unsigned char used = 0;
    vaddr((unsigned int)y * SCR_W + x);
    last_attr = 0xFF;
    for (j = n - 1; j >= 0; j--) { put_cell((unsigned char)buf[j], a); used++; }
    while (used < w) { put_cell(' ', a); used++; }
}

/* ---- world state ---- */
static unsigned int  rows;              /* conduit rows generated (world distance) */
static unsigned int  steps;             /* simulation steps run */
static unsigned char craft_x = 39;      /* craft column (placeholder steering) */
static unsigned char tickrate = TICK_DEFAULT;
static unsigned char paused;

#define CRAFT_ROW  (PLAY_BOT - 1)       /* one row up from the bottom of the field */

/* What the lane looks like at screen row r. Row r was injected when `rows` was
 * (rows - r), so its appearance is a pure function of that -- no per-row buffer
 * needed yet. Step 2 replaces this with a real terrain ring buffer. */
static unsigned char lane_glyph(unsigned char r, unsigned char *a) {
    unsigned int n = rows - r;
    if ((n & 7) == 0) { *a = A_STREAM2; return G_MARK; }   /* ruler every 8 rows */
    *a = A_STREAM;
    return (n & 1) ? G_STREAM1 : G_STREAM2;                /* phase-dithered flow */
}

static void draw_craft(void) {
    vaddr((unsigned int)CRAFT_ROW * SCR_W + craft_x);
    last_attr = 0xFF;
    put_cell(G_CRAFT, A_CRAFT);
}
/* Restore the terrain the craft was covering. Must run BEFORE the scroll, or the
 * craft glyph gets shifted down with the world and smears a trail of ghosts. */
static void erase_craft(void) {
    unsigned char a, g = lane_glyph(CRAFT_ROW, &a);
    vaddr((unsigned int)CRAFT_ROW * SCR_W + craft_x);
    last_attr = 0xFF;
    put_cell(g, a);
}

/* Advance the world one row: chip-side scroll, then draw the freshly opened
 * top row. The scroll fills row 0 with the fill char in the current attr latch,
 * so blank it to "outside the conduit" and we only draw the lane itself. */
static void scroll_world(void) {
    unsigned char i, g, a;

    vattr(A_DARK);
    last_attr = A_DARK;
    vfill(' ');
    vcmd(VCMD_SCROLLDOWN);

    rows++;
    g = lane_glyph(0, &a);
    vaddr(LANE_L);
    last_attr = 0xFF;
    put_cell(G_WALL, A_WALL);
    for (i = 0; i < LANE_W; i++) put_cell(g, a);
    put_cell(G_WALL, A_WALL);
}

/* One fixed simulation step. Everything that moves is driven from here, so the
 * whole game speed is the tick divisor -- no per-object float speeds. */
static void step_world(void) {
    erase_craft();
    scroll_world();
    draw_craft();
    steps++;
}

/* ---- HUD (rows 22..24, pinned below the scroll region) ---- */
static void draw_hud_static(void) {
    unsigned char i;

    vaddr((unsigned int)HUD_ROW * SCR_W);
    last_attr = 0xFF;
    for (i = 0; i < SCR_W; i++) put_cell(G_HBAR, A_HUD);
    put_str(2, HUD_ROW, " KERNEL PANIC  v0.1  scaffold ", A_CRAFT);

    put_str(2,  HUD_ROW + 1, "TICK:",  A_HUD);
    put_str(11, HUD_ROW + 1, "TARGET:", A_HUD);
    put_str(20, HUD_ROW + 1, "/s", A_HUD);        /* static units: never redrawn */
    put_str(24, HUD_ROW + 1, "ACTUAL:", A_HUD);
    put_str(33, HUD_ROW + 1, "/s", A_HUD);
    put_str(37, HUD_ROW + 1, "STEPS:", A_HUD);
    put_str(51, HUD_ROW + 1, "ROWS:", A_HUD);

    put_str(2, HUD_ROW + 2,
            "Q quit   P pause   SPACE step   +/- speed   arrows or h/l steer",
            A_TEXT);
}
/* Only the numeric fields, and only when something actually changed -- see the
 * hud_dirty flag in main(). Redrawing this every pass of the loop would burn far
 * more time on redundant video writes than the simulation itself uses. */
static void draw_hud_live(unsigned char measured) {
    put_num(7,  HUD_ROW + 1, tickrate, 2, A_TEXT);
    put_num(18, HUD_ROW + 1, 60u / tickrate, 2, A_TEXT);
    put_num(31, HUD_ROW + 1, measured, 2,
            (measured + 1u >= 60u / tickrate) ? A_TEXT : A_WARN);
    put_num(43, HUD_ROW + 1, steps, 5, A_TEXT);
    put_num(56, HUD_ROW + 1, rows, 5, A_TEXT);
    put_str(64, HUD_ROW + 1, paused ? "[PAUSED]" : "        ", A_WARN);
}

/* ---- input ----
 * Non-blocking decode with a persistent ESC state machine: in a real-time loop
 * an arrow's three bytes (ESC [ A) can arrive across separate passes, so we
 * can't peek for them synchronously. A bare ESC never becomes an action --
 * every arrow starts with one, so it's ambiguous by construction. */
static unsigned char esc_state;

static int getkey(void) {
    int c = INCH_NB();
    if (c < 0) return -1;
    switch (esc_state) {
        case 0:
            if (c == 0x1B) { esc_state = 1; return -1; }
            return c;
        case 1:
            esc_state = (c == '[') ? 2 : 0;
            return -1;
        default:
            esc_state = 0;
            switch (c) {
                case 'A': return 'k';
                case 'B': return 'j';
                case 'C': return 'l';
                case 'D': return 'h';
            }
            return -1;
    }
}

/* Returns 0 to quit. */
static unsigned char handle_key(int k) {
    switch (k) {
        case 'Q': case 'q':
            return 0;
        case 'P': case 'p':
            paused ^= 1;
            break;
        case ' ':                       /* single-step while paused: frame debugging */
            if (paused) { step_world(); }
            break;
        case '+': case '=':             /* smaller divisor = faster world */
            if (tickrate > TICK_MIN) tickrate--;
            break;
        case '-': case '_':
            if (tickrate < TICK_MAX) tickrate++;
            break;
        case 'h':
            if (craft_x > LANE_L + 1) { erase_craft(); craft_x--; draw_craft(); }
            break;
        case 'l':
            if (craft_x < LANE_R - 1) { erase_craft(); craft_x++; draw_craft(); }
            break;
        default:
            break;
    }
    return 1;
}

void main(void) {
    unsigned int  last, now;
    unsigned char catchup, sec, persec = 0, measured = 0, hud_dirty = 1;
    int k;

    rngv = rng_seed();
    if (rngv == 0) rngv = 0xACE1;       /* xorshift must never start at zero */

    vhidecur();
    vattr(A_TEXT); vaddr(0); vfill(' '); vcmd(VCMD_CLEAR);
    vscrollbot(PLAY_BOT);               /* AFTER the clear -- a clear resets this */

    draw_hud_static();
    draw_craft();

    sec = rtc_sec();
    last = jiffies();

    for (;;) {
        /* --- fixed-timestep accumulator, all integer. Unsigned subtraction
         * makes the counter's ~18-minute wrap harmless. --- */
        now = jiffies();
        if (!paused && (unsigned int)(now - last) >= tickrate) {
            catchup = 0;
            while ((unsigned int)(now - last) >= tickrate && catchup < MAX_CATCHUP) {
                step_world();
                last += tickrate;
                persec++;
                catchup++;
            }
            /* Still behind after the catch-up cap? Drop the backlog instead of
             * spinning forever trying to make it up. */
            if ((unsigned int)(now - last) >= tickrate) last = now;
            hud_dirty = 1;              /* steps/rows advanced */
        }

        /* Measure real steps-per-second off the RTC -- this is the readout that
         * proves the loop is actually pacing at the target rate. */
        if (rtc_sec() != sec) {
            sec = rtc_sec();
            measured = persec;
            persec = 0;
            hud_dirty = 1;
        }

        if (hud_dirty) { draw_hud_live(measured); hud_dirty = 0; }

        /* Poll input every pass, not just on tick boundaries, so steering stays
         * responsive independently of the world's scroll cadence. */
        k = getkey();
        if (k >= 0) {
            if (!handle_key(k)) break;
            hud_dirty = 1;              /* pause/speed may have changed */
        }

        if (paused) last = jiffies();   /* don't bank a backlog while paused */
    }

    QUITDOS();
}
