/* ============================================================================
 * kpanic.c -- KERNEL PANIC: engine (build step 2).
 *
 * Working: the fixed-tick simulation loop paced off the kernel's 60 Hz jiffy
 * counter, the bounded scroll region with a pinned HUD, procedurally generated
 * meandering conduit terrain (with gauntlets and islands) held in a per-row ring
 * buffer, wall/island collision against the craft, a scrolling circuit-board
 * backdrop outside the conduit, and glide steering that survives the host's key
 * auto-repeat delay.
 *
 * Deliberately NOT here yet (see DESIGN.md build path): energy + OVERCLOCK and
 * firing (step 3), enemies (4), power-ups (5), bosses (6), juice/scoring (7).
 * A collision just counts a CRASH, pops the craft cell, and re-centers you --
 * there are no lives or energy to lose until step 3.
 * ==========================================================================*/
#include "kpanic.h"

/* ---- RNG: inline xorshift, seeded from the RTC (cheaper per call than the
 * K_GET_RAND_NUM ABI, which we'd otherwise hit several times per generated row) */
static unsigned int rngv = 0xACE1;
unsigned int rnd16(void) {
    rngv ^= (unsigned int)(rngv << 7);
    rngv ^= (unsigned int)(rngv >> 9);
    rngv ^= (unsigned int)(rngv << 8);
    return rngv;
}
/* n == 0 would be a divide-by-zero; callers pass computed spans, so guard here. */
unsigned char rndn(unsigned char n) {
    if (n == 0) return 0;
    return (unsigned char)(rnd16() % n);
}

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

/* ============================================================================
 * Conduit terrain
 *
 * One entry per playfield row, structure-of-arrays (cc65 generates a multiply
 * plus offset adds for every field of a struct array, so parallel arrays are
 * materially faster). Ring-buffered: `head` is the slot holding screen row 0
 * (the newest row) and decrements on each scroll, so the slot for screen row r
 * is head+r wrapped -- no shifting 88 bytes every step, and no modulo either
 * since r < PLAY_H means head+r < 2*PLAY_H.
 * ==========================================================================*/
static unsigned char r_lx[PLAY_H];      /* left wall column */
static unsigned char r_rx[PLAY_H];      /* right wall column */
static unsigned char r_ix[PLAY_H];      /* island start column */
static unsigned char r_iw[PLAY_H];      /* island width; 0 = no island */
static unsigned char head;

static unsigned char slot(unsigned char r) {
    unsigned char i = head + r;
    if (i >= PLAY_H) i -= PLAY_H;
    return i;
}

/* Generator state: the conduit is a meandering center with a drifting width. */
static unsigned char gen_cx = 39;       /* channel center column */
static unsigned char gen_hw = HW_MAX;   /* channel half-width */
static unsigned char gen_target = HW_MAX;
static unsigned char gen_gaunt;         /* rows left in a deliberate squeeze */
static unsigned char isl_left, isl_x, isl_w;

/* Generate one new row into the slot `head` currently points at. */
static void gen_row(void) {
    unsigned char lx, rx, span;

    /* --- width: drift one step toward the target, re-picking the target now and
     * then. A "gauntlet" is just a hard, sustained low target. --- */
    if (gen_gaunt) {
        gen_gaunt--;
    } else if (rndn(70) == 0) {
        gen_target = HW_MIN + rndn(3);          /* squeeze hard */
        gen_gaunt = 8 + rndn(10);
    } else if (rndn(22) == 0) {
        gen_target = HW_MIN + rndn(HW_MAX - HW_MIN + 1);
    }
    if (gen_hw < gen_target) gen_hw++;
    else if (gen_hw > gen_target) gen_hw--;

    /* --- center: meander at most one column per row, so the walls read as a
     * continuous snaking edge rather than a jagged mess. --- */
    if (rndn(3) == 0) {
        if (rnd16() & 1) {
            if (gen_cx > (unsigned char)(WALL_MIN_X + gen_hw)) gen_cx--;
        } else {
            if (gen_cx < (unsigned char)(WALL_MAX_X - gen_hw)) gen_cx++;
        }
    }
    /* A widening channel can push a wall off screen; pull the center back in.
     * Bounded: HW_MAX leaves a valid center range, so this can't spin. */
    while ((int)gen_cx - (int)gen_hw < WALL_MIN_X) gen_cx++;
    while ((int)gen_cx + (int)gen_hw > WALL_MAX_X) gen_cx--;

    lx = gen_cx - gen_hw;
    rx = gen_cx + gen_hw;
    r_lx[head] = lx;
    r_rx[head] = rx;

    /* --- islands: mid-channel obstacles, only where there's room to pass on
     * both sides, persisting a few rows so they read as a solid pillar. --- */
    if (isl_left) {
        isl_left--;
    } else if (gen_hw >= ISL_MIN_HW && rndn(40) == 0) {
        isl_w = 2 + rndn(3);
        span = (rx - lx - 1) - 6 - isl_w;       /* leave >=3 open each side */
        isl_x = lx + 4 + rndn(span);
        isl_left = 4 + rndn(8);
    }
    /* Re-validate every row: the channel may have narrowed or shifted out from
     * under a still-running island, which would fuse it to a wall. */
    if (isl_left && isl_x > lx + 2 && isl_x + isl_w < rx - 2) {
        r_ix[head] = isl_x;
        r_iw[head] = isl_w;
    } else {
        r_ix[head] = 0;
        r_iw[head] = 0;
    }
}

/* Is column x blocked at screen row r (wall or island)? */
static unsigned char blocked(unsigned char r, unsigned char x) {
    unsigned char i = slot(r);
    if (x <= r_lx[i] || x >= r_rx[i]) return 1;
    if (r_iw[i] && x >= r_ix[i] && x < (unsigned char)(r_ix[i] + r_iw[i])) return 1;
    return 0;
}

static unsigned int rows;               /* conduit rows generated (world distance) */

/* Which columns carry a vertical trace. Built once at startup with irregular
 * spacing and the odd adjacent pair, because evenly spaced traces read as graph
 * paper rather than routed copper. Stable in x, so the traces are continuous
 * lines down the board. */
static unsigned char board_col[SCR_W];

static void board_init(void) {
    unsigned char x = 1;
    while (x < SCR_W) {
        board_col[x] = 1;
        if (rndn(4) == 0 && x + 1 < SCR_W) { board_col[x + 1] = 1; x++; }
        x += 2 + rndn(4);
    }
}

/* Appearance of one circuit-board cell outside the conduit. Everything keys off
 * the WORLD row so the board scrolls with the conduit rather than standing still. */
static void board_cell(unsigned char x, unsigned int wy,
                       unsigned char *g, unsigned char *a) {
    *a = A_BOARD;
    if ((wy & 15) == 3) {                       /* a horizontal run every 16 rows */
        *g = board_col[x] ? G_VIA : G_TRACE_H;
    } else if (board_col[x]) {
        *g = ((wy % 24) == 0) ? G_PAD : G_TRACE_V;
    } else {
        *g = ' ';
    }
}

/* Appearance of one cell inside the conduit: the very same board routing, just
 * recessed into shadow -- so the channel reads as a trench cut into one
 * continuous board, with the traces lining up across the wall.
 *
 * Two earlier attempts here were both wrong for instructive reasons: a solid
 * dithered fill read unmistakably as water, and sparse dots on black read just as
 * unmistakably as a starfield. Reusing the board pattern avoids inventing a third
 * visual language, and it can't fall out of alignment with the board by design. */
static void lane_cell(unsigned char x, unsigned int wy,
                      unsigned char *g, unsigned char *a) {
    board_cell(x, wy, g, a);
    *a = A_RECESS;
}

/* Draw screen row r from its ring slot, as one contiguous 80-cell stream:
 *
 *   [circuit board] [bevel] [WALL] [ ... data stream ... ] [WALL] [bevel] [board]
 *
 * The board fills what used to be dead blank space, which is what made the walls
 * read as floating blocks. Its grid phase keys off the WORLD row, not the screen
 * row, so the board scrolls with the conduit instead of standing still while
 * everything else moves.
 *
 * Each row is painted once when it enters and then just rides the hardware scroll
 * down, so terrain costs one row of writes per step, never a full repaint. */
static void draw_row(unsigned char r) {
    unsigned char i = slot(r);
    unsigned char lx = r_lx[i], rx = r_rx[i], iw = r_iw[i], ix = r_ix[i];
    unsigned char x, g, a;
    unsigned int  wy = rows - r;                /* world row of this screen row */

    vaddr((unsigned int)r * SCR_W);
    last_attr = 0xFF;
    for (x = 0; x < SCR_W; x++) {
        if (x == lx || x == rx) {
            put_cell(G_WALL, A_WALL);
        } else if (x == (unsigned char)(lx - 1) || x == (unsigned char)(rx + 1)) {
            put_cell(G_BEVEL, A_BEVEL);
        } else if (x > lx && x < rx) {
            if (iw && x >= ix && x < (unsigned char)(ix + iw)) {
                put_cell(G_WALL, A_WALL);
            } else {
                lane_cell(x, wy, &g, &a);
                put_cell(g, a);
            }
        } else {
            board_cell(x, wy, &g, &a);
            put_cell(g, a);
        }
    }
}

/* ---- world state ---- */
static unsigned char craft_x = 39;
static unsigned char tickrate = TICK_DEFAULT;
static unsigned char paused;
static unsigned int  crashes;
static unsigned char flash;             /* frames left showing the impact pop */
static signed char   glide_dx;          /* -1 / +1: direction of the current drift */
static unsigned char glide_left;        /* ticks of drift remaining */
static unsigned char since_steer = 255; /* ticks since the last steer key */

#define CRAFT_ROW  (PLAY_BOT - 1)

static void draw_craft(void) {
    vaddr((unsigned int)CRAFT_ROW * SCR_W + craft_x);
    last_attr = 0xFF;
    if (flash) put_cell(G_BOOM, (unsigned char)(A_WARN | 0x80));  /* reverse video */
    else       put_cell(G_CRAFT, A_CRAFT);
}
/* Restore the terrain under the craft. Must run BEFORE the scroll, or the craft
 * glyph gets shifted down with the world and smears a trail of ghosts. */
static void erase_craft(void) {
    unsigned char i = slot(CRAFT_ROW);
    unsigned char g, a;

    if (r_iw[i] && craft_x >= r_ix[i] && craft_x < (unsigned char)(r_ix[i] + r_iw[i])) {
        g = G_WALL; a = A_WALL;
    } else if (craft_x <= r_lx[i] || craft_x >= r_rx[i]) {
        g = G_WALL; a = A_WALL;
    } else {
        lane_cell(craft_x, rows - CRAFT_ROW, &g, &a);   /* same helper draw_row uses,
                                                         * so they can't drift apart */
    }
    vaddr((unsigned int)CRAFT_ROW * SCR_W + craft_x);
    last_attr = 0xFF;
    put_cell(g, a);
}

/* Impact: count it, pop the cell, and re-center in the channel. Step 3 turns
 * this into real damage against the shared ENERGY pool. */
static void crash(void) {
    unsigned char i = slot(CRAFT_ROW);
    crashes++;
    flash = 3;
    glide_left = 0;                     /* don't drift straight back into the wall */
    craft_x = (unsigned char)((r_lx[i] + r_rx[i]) >> 1);
}

/* Advance the world one row, then re-test collision: the terrain can close on a
 * stationary craft, which has to be just as lethal as steering into a wall. */
static void scroll_world(void) {
    vattr(A_DARK);
    last_attr = A_DARK;
    vfill(' ');
    vcmd(VCMD_SCROLLDOWN);

    head = head ? (unsigned char)(head - 1) : (unsigned char)(PLAY_H - 1);
    rows++;
    gen_row();
    draw_row(0);
}

/* One fixed simulation step: the single place anything moves, so world speed is
 * purely the tick divisor -- no per-object fractional speeds. */
static void step_world(void) {
    erase_craft();                      /* before the scroll, or it smears a ghost */
    scroll_world();
    if (flash) flash--;
    if (since_steer < 255) since_steer++;   /* how stale the last steer key is */
    /* Continue the steering drift -- only ever armed by an auto-repeat press, so
     * this smooths between repeats without making taps slide. */
    if (glide_left) {
        glide_left--;
        craft_x = (unsigned char)(craft_x + glide_dx);
    }
    if (blocked(CRAFT_ROW, craft_x)) crash();
    draw_craft();
}

/* ---- HUD (rows 22..24, pinned below the scroll region) ---- */
static void draw_hud_static(void) {
    unsigned char i;

    vaddr((unsigned int)HUD_ROW * SCR_W);
    last_attr = 0xFF;
    for (i = 0; i < SCR_W; i++) put_cell(G_HBAR, A_HUD);
    put_str(2, HUD_ROW, " KERNEL PANIC  v0.5  circuit ", A_CRAFT);

    put_str(2,  HUD_ROW + 1, "TICK:", A_HUD);
    put_str(10, HUD_ROW + 1, "TGT:",  A_HUD);
    put_str(16, HUD_ROW + 1, "/s",    A_HUD);   /* static units: never redrawn */
    put_str(20, HUD_ROW + 1, "ACT:",  A_HUD);
    put_str(26, HUD_ROW + 1, "/s",    A_HUD);
    put_str(30, HUD_ROW + 1, "ROWS:", A_HUD);
    put_str(42, HUD_ROW + 1, "WIDTH:", A_HUD);
    put_str(52, HUD_ROW + 1, "CRASH:", A_HUD);

    put_str(2, HUD_ROW + 2,
            "Q quit   P pause   SPACE step   +/- speed   arrows or h/l steer",
            A_TEXT);
}
/* Numeric fields only, and only when something changed (see hud_dirty): a full
 * HUD repaint every pass of the loop costs more than the simulation does. */
static void draw_hud_live(unsigned char measured) {
    unsigned char i = slot(CRAFT_ROW);
    put_num(7,  HUD_ROW + 1, tickrate, 2, A_TEXT);
    put_num(14, HUD_ROW + 1, 60u / tickrate, 2, A_TEXT);
    put_num(24, HUD_ROW + 1, measured, 2,
            (measured + 1u >= 60u / tickrate) ? A_TEXT : A_WARN);
    put_num(35, HUD_ROW + 1, rows, 5, A_TEXT);
    put_num(48, HUD_ROW + 1, (unsigned int)(r_rx[i] - r_lx[i] - 1), 2,
            (r_rx[i] - r_lx[i] - 1 <= 9) ? A_WARN : A_TEXT);
    put_num(58, HUD_ROW + 1, crashes, 3, crashes ? A_WARN : A_TEXT);
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

/* Steer exactly one column. A glide is granted ONLY when this press looks like
 * auto-repeat -- a recent press the same way -- so holding a key reads as smooth
 * continuous motion while a deliberate tap stays a single precise column.
 * Moving into a wall crashes rather than being clamped: the conduit edge is
 * lethal, so bumping it has to cost you. */
static void steer(signed char dx) {
    erase_craft();
    craft_x = (unsigned char)(craft_x + dx);
    glide_left = (dx == glide_dx && since_steer <= REPEAT_WINDOW) ? REPEAT_GLIDE : 0;
    glide_dx = dx;
    since_steer = 0;
    if (blocked(CRAFT_ROW, craft_x)) crash();
    draw_craft();
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
            if (paused) step_world();
            break;
        case '+': case '=':             /* smaller divisor = faster world */
            if (tickrate > TICK_MIN) tickrate--;
            break;
        case '-': case '_':
            if (tickrate < TICK_MAX) tickrate++;
            break;
        case 'h': steer(-1); break;
        case 'l': steer(1);  break;
        default:
            break;
    }
    return 1;
}

void main(void) {
    unsigned int  last, now;
    unsigned char catchup, sec, persec = 0, measured = 0, hud_dirty = 1, r;
    int k;

    rngv = rng_seed();
    if (rngv == 0) rngv = 0xACE1;       /* xorshift must never start at zero */

    vhidecur();
    vattr(A_TEXT); vaddr(0); vfill(' '); vcmd(VCMD_CLEAR);
    vscrollbot(PLAY_BOT);               /* AFTER the clear -- a clear resets this */
    board_init();                       /* fixed trace routing for the whole run */

    /* Fill the playfield so the run opens inside a conduit rather than a void.
     * Generating downward from the top keeps `rows` consistent with the dither
     * phase that rows_parity() derives. */
    for (r = 0; r < PLAY_H; r++) {
        head = head ? (unsigned char)(head - 1) : (unsigned char)(PLAY_H - 1);
        rows++;
        gen_row();
    }
    for (r = 0; r < PLAY_H; r++) draw_row(r);

    craft_x = (unsigned char)((r_lx[slot(CRAFT_ROW)] + r_rx[slot(CRAFT_ROW)]) >> 1);

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
            hud_dirty = 1;
        }

        /* Measure real steps-per-second off the RTC -- the readout that proves
         * the loop is actually pacing at the target rate. */
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
            hud_dirty = 1;
        }

        if (paused) last = jiffies();   /* don't bank a backlog while paused */
    }

    QUITDOS();
}
