/* ============================================================================
 * kpanic.c -- KERNEL PANIC: engine (build step 5).
 *
 * Working: the fixed-tick simulation loop paced off the kernel's 60 Hz jiffy
 * counter; the bounded scroll region with a pinned HUD; procedurally generated
 * meandering conduit terrain (gauntlets, islands, data nodes) in a per-row ring
 * buffer; a scrolling circuit-board backdrop; and the shared ENERGY pool with
 * firing and OVERCLOCK.
 *
 * Controls come from the PIA's live key-state port ($FE0F), sampled once per
 * tick, NOT from the keystroke buffer. That stream carries no key-up, so it
 * cannot express "held": movement could only be inferred from OS auto-repeat
 * (~500ms stall) and firing cancelled steering outright, because the host only
 * repeats the most recently pressed key. Polling independent bits fixes both.
 *
 * The ENERGY pool is the game's whole point (see DESIGN.md): it drains on its
 * own, refills only by flying over a data node, and OVERCLOCK burns it fast. A
 * node can be TAKEN for energy or SHOT for score -- never both -- so every node
 * is a bet on how long you intend to live.
 *
 * Corruption (step 4) is the first thing that does not ride the terrain ring:
 * daemons close on you, worms weave, sentinels hold station and shoot. They move
 * relative to the world rather than with it, so they carry their own pools and
 * explicit erase/redraw.
 *
 * Power-ups (step 5) are the weapon chain: fragments dropped by kills swap the
 * gun's character and deepen it, and a crash knocks a level back off.
 *
 * The playfield is 80 columns by 24 ordinary rows. It was briefly a 40x12 band of
 * double-size rows to make the glyphs bigger; that halved the runway and doubled the
 * scroll quantum to 32 px, which lurched. See the geometry note in kpanic.h.
 *
 * Deliberately NOT here yet: bosses + sector progression (step 6), juice and the
 * real scoring/outcome tally (step 7), balance and the manual (step 8).
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
 * is head+r wrapped -- no shifting bytes every step, and no modulo either since
 * r < PLAY_H means head+r < 2*PLAY_H.
 *
 * Data nodes live in this ring too, so they ride the hardware scroll for free
 * exactly like the walls do -- no separate moving-object bookkeeping.
 * ==========================================================================*/
static unsigned char r_lx[PLAY_H];      /* left wall column */
static unsigned char r_rx[PLAY_H];      /* right wall column */
static unsigned char r_ix[PLAY_H];      /* island start column */
static unsigned char r_iw[PLAY_H];      /* island width; 0 = no island */
static unsigned char r_nx[PLAY_H];      /* data node column; 0 = none */
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
    unsigned char lx, rx, span, nx;

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
        /* Short: 2-4 rows. Longer pillars read as a second wall rather than an
         * obstacle to weave around, and they block the channel for so long that
         * dodging becomes committing rather than reacting. */
        isl_left = 1 + rndn(3);
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

    /* --- data nodes: the only way to refill energy. Rare enough that you can't
     * take every one for granted, common enough to survive on. NODE_W cells wide,
     * placed at least one clear column inside each wall. --- */
    r_nx[head] = 0;
    if (rndn(90) == 0) {
        span = rx - lx - NODE_W - 2;            /* valid start columns */
        if (span > 0) {
            nx = lx + 2 + rndn(span);
            /* reject if any of its cells would sit inside the island */
            if (!(r_iw[head] &&
                  nx < (unsigned char)(r_ix[head] + r_iw[head]) &&
                  (unsigned char)(nx + NODE_W) > r_ix[head]))
                r_nx[head] = nx;
        }
    }
}

/* Does column x fall on the node in ring slot i? */
static unsigned char on_node(unsigned char i, unsigned char x) {
    return r_nx[i] && x >= r_nx[i] && x < (unsigned char)(r_nx[i] + NODE_W);
}

/* Is column x blocked at screen row r (wall or island)? Nodes are not solid. */
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
static unsigned char board_col[PLAY_COLS];

static void board_init(void) {
    unsigned char x = 1;
    while (x < PLAY_COLS) {
        board_col[x] = 1;
        if (rndn(4) == 0 && x + 1 < PLAY_COLS) { board_col[x + 1] = 1; x++; }
        x += 2 + rndn(4);
    }
}

/* Appearance of one circuit-board cell. Everything keys off the WORLD row so the
 * board scrolls with the conduit rather than standing still. */
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

/* Inside the conduit it's the very same board routing, just recessed into
 * shadow -- so the channel reads as a trench cut into one continuous board, with
 * the traces lining up across the wall.
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

/* THE single source of truth for what any playfield cell looks like, ignoring
 * moving objects. draw_row() paints with it and every erase restores with it, so
 * the two can never disagree -- which is what stops erased craft/shots from
 * leaving holes or smears in the terrain. */
static void terrain_cell(unsigned char r, unsigned char x,
                         unsigned char *g, unsigned char *a) {
    unsigned char i = slot(r);
    unsigned int  wy = rows - r;

    if (x == r_lx[i] || x == r_rx[i]) {
        *g = G_WALL; *a = A_WALL;
    } else if (x == (unsigned char)(r_lx[i] - 1) ||
               x == (unsigned char)(r_rx[i] + 1)) {
        *g = G_BEVEL; *a = A_BEVEL;
    } else if (x > r_lx[i] && x < r_rx[i]) {
        if (r_iw[i] && x >= r_ix[i] && x < (unsigned char)(r_ix[i] + r_iw[i])) {
            *g = G_WALL; *a = A_WALL;
        } else if (on_node(i, x)) {
            *g = G_NODE; *a = A_NODE;
        } else {
            lane_cell(x, wy, g, a);
        }
    } else {
        board_cell(x, wy, g, a);
    }
}

/* Paint one cell of terrain (used to erase a moving object). */
static void restore_cell(unsigned char r, unsigned char x) {
    unsigned char g, a;
    terrain_cell(r, x, &g, &a);
    vaddr(PROW(r) * SCR_W + x);
    last_attr = 0xFF;
    put_cell(g, a);
}

/* Draw screen row r as one contiguous 80-cell stream:
 *   [board] [bevel] [WALL] [ ... channel ... ] [WALL] [bevel] [board]
 * Each row is painted once when it enters and then just rides the hardware
 * scroll down, so terrain costs one row of writes per step, never a repaint.
 *
 * This deliberately inlines what terrain_cell() does rather than calling it 80
 * times. Every per-cell function call costs real cycles under cc65, and the
 * `wy % 24` pad test is a 16-bit modulo -- a subroutine call -- so hoisting it
 * out of the loop matters more than all the rest combined. Written the tidy way
 * (one terrain_cell() call per cell) this row cost several times as much and
 * dragged the whole frame rate down with it.
 *
 * MUST stay in agreement with terrain_cell() below, which serves the same
 * pixels for single-cell restores. */
static void draw_row(unsigned char r) {
    unsigned char i = slot(r);
    unsigned char lx = r_lx[i], rx = r_rx[i], ix = r_ix[i], iw = r_iw[i], nx = r_nx[i];
    unsigned int  wy = rows - r;
    unsigned char trace_row = ((wy & 15) == 3);     /* hoisted: cheap mask */
    unsigned char pad_row   = ((wy % 24) == 0);     /* hoisted: expensive modulo */
    unsigned char x, g, a;

    vaddr(PROW(r) * SCR_W);
    last_attr = 0xFF;
    for (x = 0; x < PLAY_COLS; x++) {
        if (x == lx || x == rx) {
            g = G_WALL; a = A_WALL;
        } else if (x == (unsigned char)(lx - 1) || x == (unsigned char)(rx + 1)) {
            g = G_BEVEL; a = A_BEVEL;
        } else if (x > lx && x < rx) {
            if (iw && x >= ix && x < (unsigned char)(ix + iw)) {
                g = G_WALL; a = A_WALL;
            } else if (nx && x >= nx && x < (unsigned char)(nx + NODE_W)) {
                g = G_NODE; a = A_NODE;
            } else {                                /* recessed board in the channel */
                a = A_RECESS;
                if (trace_row)         g = board_col[x] ? G_VIA : G_TRACE_H;
                else if (board_col[x]) g = pad_row ? G_PAD : G_TRACE_V;
                else                   g = ' ';
            }
        } else {                                    /* board outside the conduit */
            a = A_BOARD;
            if (trace_row)         g = board_col[x] ? G_VIA : G_TRACE_H;
            else if (board_col[x]) g = pad_row ? G_PAD : G_TRACE_V;
            else                   g = ' ';
        }
        put_cell(g, a);
    }
}

/* ---- world state ---- */
static unsigned char craft_x = 39;
static unsigned char tickrate = TICK_DEFAULT;
static unsigned char paused;
static unsigned char flash;             /* frames left showing the impact pop */

static unsigned int  energy = ENERGY_MAX;
static unsigned int  score;
static unsigned int  crashes;
static unsigned char overclock;
static unsigned char cooldown;          /* ticks until the gun can fire again */
static unsigned char dead;

/* shots: structure-of-arrays, fixed pool, no allocation */
static unsigned char s_x[MAX_SHOTS], s_y[MAX_SHOTS], s_live[MAX_SHOTS];
static unsigned char s_from[MAX_SHOTS];     /* row this shot started the tick on */
static unsigned char s_pierce[MAX_SHOTS];   /* hits left before the shot dies */
static unsigned char s_home[MAX_SHOTS];     /* 1 = drifts toward a target */

/* The gun: a kind and a level. Collecting the same fragment again deepens it;
 * a different one swaps you to that kind at level 1 -- so a pickup is a real
 * decision when you are already deep in something else. */
static unsigned char weapon = W_PLAIN;
static unsigned char wlevel = 1;

/* Fragments drift down with the world like a node, but they are objects rather
 * than terrain because they are dropped mid-run, not generated with a row. */
static unsigned char f_live[MAX_FRAGS];
static unsigned char f_x[MAX_FRAGS], f_y[MAX_FRAGS], f_kind[MAX_FRAGS];

static unsigned char frag_glyph(unsigned char k) {
    if (k == W_SPREAD) return 'S';
    if (k == W_BEAM)   return 'B';
    return 'H';
}

/* A LOGICAL row. It used to be derived from the band's physical bottom, which was
 * the same number back when the playfield was 22 single rows -- with an 11-row
 * double band it indexed the terrain ring out of bounds and drew off-screen. */
#define CRAFT_ROW  (PLAY_H - 1)

/* Did an object sweep over the craft on its way from row `from` to row `to`?
 *
 * Nothing here moves exactly one row per tick. The world scrolls two under
 * OVERCLOCK, a daemon adds one on top of that, and a pellet adds one again -- so
 * testing `y == CRAFT_ROW` AFTER the move silently misses anything that stepped
 * straight over the craft. The effect was backwards from the design: OVERCLOCK,
 * the reckless mode you are meant to pay for, became the safest one, because
 * pellets and daemons flew through you untouched. Anything that can hit the craft
 * has to be resolved across the span it travelled, not at its landing row. */
static unsigned char swept_craft(unsigned char from, unsigned char to,
                                 unsigned char x) {
    return (unsigned char)(from <= CRAFT_ROW && to >= CRAFT_ROW &&
                           x == craft_x);
}

static void draw_craft(void) {
    vaddr(PROW(CRAFT_ROW) * SCR_W + craft_x);
    last_attr = 0xFF;
    if (flash) put_cell(G_BOOM, (unsigned char)(A_WARN | 0x80));  /* reverse video */
    else       put_cell(G_CRAFT, A_CRAFT);
}

/* ---- energy ---- */
static void energy_spend(unsigned int amount) {
    if (energy <= amount) { energy = 0; dead = 1; }
    else                  energy -= amount;
}
static void energy_gain(unsigned int amount) {
    energy += amount;
    if (energy > ENERGY_MAX) energy = ENERGY_MAX;
}

/* Impact: costs energy rather than just counting. Hitting the conduit is now a
 * real bite out of your lifespan, which is what makes the gauntlets matter. */
static void crash(void) {
    unsigned char i = slot(CRAFT_ROW);
    crashes++;
    flash = 3;
    craft_x = (unsigned char)((r_lx[i] + r_rx[i]) >> 1);
    energy_spend(ENERGY_CRASH);
    /* The impact shakes the gun down a level. Energy is the obvious cost of a
     * crash; losing hard-won firepower is the one that actually stings. */
    if (wlevel > 1) wlevel--;
}

/* ---- weapon ---- */
/* Put one shot in the air at column x, if a slot is free. */
static void shot_spawn(unsigned char x) {
    unsigned char i;
    for (i = 0; i < MAX_SHOTS; i++) {
        if (!s_live[i]) {
            s_live[i] = 1;
            s_x[i] = x;
            s_y[i] = CRAFT_ROW - 1;
            /* Beam punches through one extra target per level; everything else
             * is consumed by the first thing it hits. */
            s_pierce[i] = (weapon == W_BEAM) ? (unsigned char)(wlevel + 1) : 1;
            s_home[i]   = (weapon == W_HOMING) ? 1 : 0;
            return;
        }
    }
}

static void fire(void) {
    if (cooldown) return;
    cooldown = overclock ? FIRE_COOLDOWN_OC : FIRE_COOLDOWN;

    shot_spawn(craft_x);
    if (weapon == W_SPREAD) {
        /* Level 1-2 covers three columns, level 3 covers five. Width is the
         * clearest way to show a level on a character grid -- you can see it. */
        if (craft_x > 1)          shot_spawn(craft_x - 1);
        if (craft_x < PLAY_COLS - 2)  shot_spawn(craft_x + 1);
        if (wlevel >= 3) {
            if (craft_x > 2)         shot_spawn(craft_x - 2);
            if (craft_x < PLAY_COLS - 3) shot_spawn(craft_x + 2);
        }
    }
    /* Deeper levels of the other kinds fire faster instead of wider. */
    if (weapon != W_SPREAD && wlevel > 1 && cooldown > 1) cooldown--;
}

/* Resolve a shot arriving at one cell. Returns 1 if the shot is consumed.
 * Checked per single-cell substep rather than at the destination, so a fast shot
 * can't tunnel through a node or a wall. */
/* Defined with the corruption pool below; declared here because shot collision
 * has to resolve against enemies and the weapon code comes first. */
static unsigned char enemy_at(unsigned char r, unsigned char x);
static void          enemy_damage(unsigned char idx);

/* 0 = nothing here, 1 = hit something destructible (a beam may punch on through),
 * 2 = hit the conduit wall, which stops anything. The wall has to be a distinct
 * answer: a beam that spent a pierce on it would carry on straight out through
 * the side of the channel. */
static unsigned char shot_hits(unsigned char r, unsigned char x) {
    unsigned char i = slot(r), nx, k, e;

    /* Corruption first: an enemy sitting in front of a wall must absorb the shot
     * rather than the wall eating it. */
    e = enemy_at(r, x);
    if (e) {
        enemy_damage(e - 1);
        return 1;
    }

    if (on_node(i, x)) {                /* score, but you forfeit the refill */
        nx = r_nx[i];
        r_nx[i] = 0;                    /* clear first, so the restore paints lane */
        score += SCORE_NODE;
        for (k = 0; k < NODE_W; k++) restore_cell(r, nx + k);
        return 1;
    }
    return blocked(r, x) ? 2 : 0;
}

/* ============================================================================
 * Corruption
 *
 * These are the first objects that do NOT ride the terrain ring. A node scrolls
 * for free because it lives in a row; an enemy has to be moved, erased and
 * redrawn by hand, because it moves relative to the world rather than with it.
 *
 * All screen-row motion is expressed as `scrolled + drift`, where `scrolled` is
 * how far the world moved this tick (2 while overclocked). Drift 0 therefore
 * means "stays put in the conduit" and drift 1 means "closes on the player".
 * ==========================================================================*/
static unsigned char e_type[MAX_ENEMIES];   /* E_NONE = free slot */
static unsigned char e_x[MAX_ENEMIES], e_y[MAX_ENEMIES];
/* The row this enemy started the tick on, and the column it descended in (before
 * a worm's weave). Needed to resolve shot collisions across the span it swept --
 * see shots_enemies_resolve(). */
static unsigned char e_from[MAX_ENEMIES], e_fx[MAX_ENEMIES];
static unsigned char e_hp[MAX_ENEMIES];
static unsigned char e_t[MAX_ENEMIES];      /* fire timer / weave phase */

static unsigned char p_live[MAX_PELLETS];
static unsigned char p_x[MAX_PELLETS], p_y[MAX_PELLETS];

static unsigned char spawn_timer = SPAWN_MIN;

/* Short-lived impact markers, so a hit has a visible signature. A kill used to
 * just stop drawing the target, which is indistinguishable from the shot having
 * gone straight through -- the thing the player was already suspicious of. */
static unsigned char pop_x[MAX_POPS], pop_y[MAX_POPS], pop_t[MAX_POPS];

static void pop_add(unsigned char x, unsigned char y) {
    unsigned char i;
    for (i = 0; i < MAX_POPS; i++) {
        if (!pop_t[i]) { pop_t[i] = POP_TICKS; pop_x[i] = x; pop_y[i] = y; return; }
    }
}

static unsigned char enemy_glyph(unsigned char t) {
    if (t == E_DAEMON)   return G_DAEMON;
    if (t == E_WORM)     return G_WORM;
    return G_SENTINEL;
}
static unsigned char enemy_attr(unsigned char t) {
    return (t == E_WORM) ? A_FOE2 : A_FOE;
}

/* Index+1 of a live enemy occupying (r,x), or 0. */
static unsigned char enemy_at(unsigned char r, unsigned char x) {
    unsigned char i;
    for (i = 0; i < MAX_ENEMIES; i++)
        if (e_type[i] && e_y[i] == r &&
            x >= e_x[i] && x < (unsigned char)(e_x[i] + ENEMY_W)) return i + 1;
    return 0;
}

static void frag_drop(unsigned char x, unsigned char y) {
    unsigned char f;
    for (f = 0; f < MAX_FRAGS; f++) {
        if (!f_live[f]) {
            f_live[f] = 1;
            f_x[f] = x;
            f_y[f] = y;
            f_kind[f] = (unsigned char)(W_SPREAD + rndn(3));   /* S, B or H */
            return;
        }
    }
}

static void enemy_kill(unsigned char i) {
    if (e_type[i] == E_DAEMON)        score += SCORE_DAEMON;
    else if (e_type[i] == E_WORM)     score += SCORE_WORM;
    else                              score += SCORE_SENTINEL;
    e_type[i] = E_NONE;
    if (rndn(FRAG_CHANCE) == 0) frag_drop(e_x[i], e_y[i]);
    pop_add(e_x[i], e_y[i]);
    restore_cell(e_y[i], e_x[i]);
    restore_cell(e_y[i], (unsigned char)(e_x[i] + 1));
}

static void enemy_damage(unsigned char idx) {
    if (--e_hp[idx] == 0) enemy_kill(idx);
    else pop_add(e_x[idx], e_y[idx]);   /* wounded: show that it landed */
}

/* Spawn one enemy at the top of the channel. Placed inside the walls with a
 * clear column either side, so it never appears already embedded in terrain. */
static void spawn_enemy(void) {
    unsigned char i, lx, rx, span, t;

    for (i = 0; i < MAX_ENEMIES; i++) if (!e_type[i]) break;
    if (i == MAX_ENEMIES) return;               /* pool full: skip this spawn */

    lx = r_lx[head];
    rx = r_rx[head];
    span = rx - lx - 2 - ENEMY_W;
    if (span == 0 || span > PLAY_COLS) return;  /* channel too tight to place one */

    t = 1 + rndn(3);
    e_type[i] = t;
    e_x[i] = lx + 2 + rndn(span);
    e_y[i] = 0;
    e_hp[i] = (t == E_DAEMON) ? HP_DAEMON : (t == E_WORM ? HP_WORM : HP_SENTINEL);
    e_t[i] = (t == E_SENTINEL) ? SENTINEL_FIRE : (rnd16() & 1);
}

static void pellet_spawn(unsigned char x, unsigned char y) {
    unsigned char i;
    for (i = 0; i < MAX_PELLETS; i++) {
        if (!p_live[i]) {
            p_live[i] = 1;
            p_x[i] = x;
            p_y[i] = y;
            return;
        }
    }
}

static void enemies_advance(unsigned char scrolled) {
    unsigned char i, ny, nx;

    for (i = 0; i < MAX_ENEMIES; i++) {
        if (!e_type[i]) continue;

        e_from[i] = e_y[i];
        e_fx[i]   = e_x[i];             /* the column it descends in, pre-weave */

        ny = e_y[i] + scrolled;
        if (e_type[i] == E_DAEMON) ny++;        /* closes faster than the world */

        /* Corruption you fly into: costs energy and dies, with you passing
         * through -- there is no bouncing off, so a missed dodge is always paid
         * for. Resolved here, across the span it just travelled, and BEFORE the
         * off-bottom test, or a daemon moving three rows could step over the
         * craft and out of the world in the same tick. */
        if (swept_craft(e_y[i], ny, e_x[i])) {
            e_type[i] = E_NONE;
            flash = 3;
            energy_spend(ENERGY_HIT);
            continue;
        }

        if (ny > PLAY_LAST) { e_type[i] = E_NONE; continue; }  /* off the bottom */
        e_y[i] = ny;

        if (e_type[i] == E_WORM) {
            /* Weave one column at a time, reversing at the walls rather than
             * grinding along them. */
            nx = e_t[i] ? e_x[i] + 1 : e_x[i] - 1;
            if (blocked(e_y[i], nx) ||
                blocked(e_y[i], (unsigned char)(nx + ENEMY_W - 1))) e_t[i] ^= 1;
            else                                                    e_x[i] = nx;
        } else if (e_type[i] == E_SENTINEL) {
            if (e_t[i]) e_t[i]--;
            else { pellet_spawn(e_x[i], e_y[i] + 1); e_t[i] = SENTINEL_FIRE; }
        }

        /* Crushed by a narrowing channel. Corruption is not privileged over the
         * conduit -- if the walls close on it, it dies like anything else. */
        if (blocked(e_y[i], e_x[i]) ||
            blocked(e_y[i], (unsigned char)(e_x[i] + 1))) e_type[i] = E_NONE;
    }
}

/* Resolve shots against corruption ACROSS THE ROWS EACH SWEPT this tick.
 *
 * This is why almost nothing could be shot. The two close head-on, several rows
 * apiece in one tick -- a shot climbs two while a daemon descends three -- so they
 * routinely pass clean through each other without ever sharing a cell. Take a shot
 * at row 10 and an enemy at row 6: the shot substeps to 9, 8, checking as it goes,
 * finds the enemy still at 6; then the enemy moves to 8, which the shot has already
 * left. Next tick the shot is at 6 and the enemy at 10. They have swapped ends and
 * nothing ever looked. VENTURE had exactly this bug between its arrow and its
 * monsters, for exactly this reason.
 *
 * Per-cell substepping cannot fix it, because the failure is not a shot skipping a
 * cell -- it is the target moving after the shot has been and gone. The fix is to
 * ask whether the two SPANS overlap. The shot swept rows [s_y, s_from] going up and
 * the enemy swept [e_from, e_y] coming down, so they met iff
 *     s_y <= e_y  &&  e_from <= s_from
 * which is interval overlap with the two "moved the right way" terms dropped as
 * always-true. */
static void shots_enemies_resolve(void) {
    unsigned char i, e;
    for (i = 0; i < MAX_SHOTS; i++) {
        if (!s_live[i]) continue;
        for (e = 0; e < MAX_ENEMIES; e++) {
            if (!e_type[e]) continue;
            /* A worm changes column mid-move; either the column it descended in
             * or the one it weaved into counts as a hit. */
            if (!(s_x[i] >= e_fx[e] && s_x[i] < (unsigned char)(e_fx[e] + ENEMY_W)) &&
                !(s_x[i] >= e_x[e]  && s_x[i] < (unsigned char)(e_x[e]  + ENEMY_W)))
                continue;
            if (s_y[i] > e_y[e] || e_from[e] > s_from[i]) continue;   /* no overlap */
            enemy_damage(e);
            if (s_pierce[i] > 1) { s_pierce[i]--; continue; }        /* beam */
            s_live[i] = 0;
            break;
        }
    }
}

/* Fragments ride the world, so they only need the scroll amount -- no drift of
 * their own. Collecting one is the decision: same kind deepens the gun, a
 * different kind swaps you back to level 1 of that kind.
 *
 * Moving and collecting are one pass because the collect test needs the row the
 * fragment came FROM as well as the one it landed on -- under OVERCLOCK the world
 * moves two rows a tick, so a fragment can pass clean over the craft. */
static void frags_step(unsigned char scrolled) {
    unsigned char f, ny;
    for (f = 0; f < MAX_FRAGS; f++) {
        if (!f_live[f]) continue;
        ny = f_y[f] + scrolled;

        if (swept_craft(f_y[f], ny, f_x[f])) {
            if (f_kind[f] == weapon) {
                if (wlevel < W_MAXLEVEL) wlevel++;
            } else {
                weapon = f_kind[f];
                wlevel = 1;
            }
            f_live[f] = 0;
            continue;
        }

        if (ny > PLAY_LAST || blocked(ny, f_x[f])) { f_live[f] = 0; continue; }
        f_y[f] = ny;
    }
}

static void pellets_advance(unsigned char scrolled) {
    unsigned char i, ny;
    for (i = 0; i < MAX_PELLETS; i++) {
        if (!p_live[i]) continue;
        ny = p_y[i] + scrolled + 1;
        /* A pellet is the fastest thing coming at you -- three rows a tick under
         * OVERCLOCK -- so it is the one that most needed resolving across its
         * span rather than at its landing row. */
        if (swept_craft(p_y[i], ny, p_x[i])) {
            p_live[i] = 0;
            flash = 3;
            energy_spend(ENERGY_PELLET);
            continue;
        }
        if (ny > PLAY_LAST || blocked(ny, p_x[i])) { p_live[i] = 0; continue; }
        p_y[i] = ny;
    }
}

static void shots_advance(void) {
    unsigned char i, step, h;
    for (i = 0; i < MAX_SHOTS; i++) {
        if (!s_live[i]) continue;
        s_from[i] = s_y[i];             /* remembered for the span test below */

        /* Homing: nudge one column toward the nearest target ahead. One column
         * per tick, so it curves rather than snapping -- a snap would make the
         * shot look like it teleported. */
        if (s_home[i]) {
            unsigned char e, best = 255, bestd = 255, d;
            for (e = 0; e < MAX_ENEMIES; e++) {
                if (!e_type[e] || e_y[e] > s_y[i]) continue;
                d = (unsigned char)(s_y[i] - e_y[e]);
                if (d < bestd) { bestd = d; best = e; }
            }
            if (best != 255) {
                if (e_x[best] < s_x[i]) s_x[i]--;
                else if (e_x[best] > s_x[i]) s_x[i]++;
            }
        }

        for (step = 0; step < SHOT_SPEED; step++) {
            if (s_y[i] == 0) { s_live[i] = 0; break; }   /* off the top */
            s_y[i]--;
            h = shot_hits(s_y[i], s_x[i]);
            if (h) {
                /* A beam spends one of its hits and keeps going -- through
                 * corruption and nodes only. The wall (h == 2) stops everything. */
                if (h == 1 && s_pierce[i] > 1) { s_pierce[i]--; continue; }
                s_live[i] = 0;
                break;
            }
        }
    }
}

/* Advance the world one row: chip-side scroll, then paint the freshly opened
 * top row. Terrain rides the scroll; moving objects are handled by the caller. */
static void scroll_world(void) {
    vattr(A_BOARD);
    last_attr = A_BOARD;
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
    unsigned char i, ks, scrolled;

    /* Erase every moving object BEFORE the scroll, or the hardware shift drags
     * their glyphs down the screen as a trail of ghosts. */
    restore_cell(CRAFT_ROW, craft_x);
    for (i = 0; i < MAX_SHOTS; i++)
        if (s_live[i]) restore_cell(s_y[i], s_x[i]);
    for (i = 0; i < MAX_ENEMIES; i++)
        if (e_type[i]) {
            restore_cell(e_y[i], e_x[i]);
            restore_cell(e_y[i], (unsigned char)(e_x[i] + 1));
        }
    for (i = 0; i < MAX_POPS; i++)
        if (pop_t[i]) {
            restore_cell(pop_y[i], pop_x[i]);
            restore_cell(pop_y[i], (unsigned char)(pop_x[i] + 1));
        }
    for (i = 0; i < MAX_PELLETS; i++)
        if (p_live[i]) restore_cell(p_y[i], p_x[i]);
    for (i = 0; i < MAX_FRAGS; i++)
        if (f_live[i]) restore_cell(f_y[i], f_x[i]);

    /* Sample the live control port ONCE per tick and act on every bit that is
     * set. Movement is therefore exactly as smooth as the tick rate, and steering
     * while firing costs nothing: the bits are independent, so there is no
     * "most recent key wins" behaviour to fight. */
    ks = keystate();
    overclock = (ks & KS_BOOST) ? 1 : 0;    /* hold to overclock, release to stop */

    scroll_world();
    scrolled = 1;
    if (overclock) { scroll_world(); scrolled = 2; }  /* overclock runs the world faster */

    if (flash) flash--;
    if (cooldown) cooldown--;
    for (i = 0; i < MAX_POPS; i++) if (pop_t[i]) pop_t[i]--;

    if (ks & KS_LEFT)  craft_x -= MOVE_PER_TICK;
    if (ks & KS_RIGHT) craft_x += MOVE_PER_TICK;
    if (ks & KS_FIRE)  fire();          /* cooldown paces it; holding is fine */

    shots_advance();
    enemies_advance(scrolled);
    shots_enemies_resolve();    /* AFTER both have moved -- see the note there */
    pellets_advance(scrolled);
    frags_step(scrolled);

    if (--spawn_timer == 0) {
        spawn_enemy();
        spawn_timer = SPAWN_MIN + rndn(SPAWN_VAR);
    }

    /* Collisions with corruption and pellets are resolved inside their own
     * advance passes now, where the row each object came from is still known --
     * see swept_craft(). Testing for them here, after everything had landed,
     * missed anything that stepped over the craft row.
     *
     * Take a node by flying over it: refill, and forfeit the score you'd have
     * got for shooting it. This is the bet the whole game is built around. */
    i = slot(CRAFT_ROW);
    if (on_node(i, craft_x)) {
        unsigned char nx = r_nx[i], k;
        r_nx[i] = 0;                    /* clear first, so the restore paints lane */
        for (k = 0; k < NODE_W; k++) restore_cell(CRAFT_ROW, nx + k);
        energy_gain(ENERGY_NODE);
    }

    if (blocked(CRAFT_ROW, craft_x)) crash();

    energy_spend(overclock ? (ENERGY_DRAIN + ENERGY_OC_DRAIN) : ENERGY_DRAIN);

    /* Redraw order is deliberate: corruption, then pellets, then your shots, then
     * the craft. Later writes win, so the things you most need to see never end
     * up hidden under something else sharing a cell. */
    /* Impact markers first, so a live object drawn over one still wins the cell. */
    for (i = 0; i < MAX_POPS; i++)
        if (pop_t[i]) {
            vaddr(PROW(pop_y[i]) * SCR_W + pop_x[i]);
            last_attr = 0xFF;
            put_cell(G_BOOM, A_SHOT);
            put_cell(G_BOOM, A_SHOT);       /* auto-increment: the second cell */
        }
    for (i = 0; i < MAX_ENEMIES; i++)
        if (e_type[i]) {
            vaddr(PROW(e_y[i]) * SCR_W + e_x[i]);
            last_attr = 0xFF;
            put_cell(enemy_glyph(e_type[i]), enemy_attr(e_type[i]));
            put_cell(enemy_glyph(e_type[i]), enemy_attr(e_type[i]));
        }
    for (i = 0; i < MAX_PELLETS; i++)
        if (p_live[i]) {
            vaddr(PROW(p_y[i]) * SCR_W + p_x[i]);
            last_attr = 0xFF;
            put_cell(G_PELLET, A_FOE);
        }
    for (i = 0; i < MAX_FRAGS; i++)
        if (f_live[i]) {
            vaddr(PROW(f_y[i]) * SCR_W + f_x[i]);
            last_attr = 0xFF;
            put_cell(frag_glyph(f_kind[i]), A_FRAG);
        }
    for (i = 0; i < MAX_SHOTS; i++)
        if (s_live[i]) {
            vaddr(PROW(s_y[i]) * SCR_W + s_x[i]);
            last_attr = 0xFF;
            put_cell(G_SHOT, A_SHOT);
        }
    draw_craft();
}

/* ---- HUD: ONE ordinary row, the last line of the screen ----
 * It was three rows, and that was the whole reason the craft looked marooned in
 * the middle of the display -- three lines of chrome plus a spare playfield row
 * put five physical rows under it. The key legend those rows carried now lives on
 * the title screen, where a player actually reads it.
 *
 * Layout, all of which must end by column 79:
 *   0 PWR   4..23 bar   25..28 energy   30 SCORE   36..40   42 ROWS   47..51
 *   53 WPN  57..62 name  63..66 Lv#   68..71 [OC]   73..77 PAUSE               */
static void draw_hud_static(void) {
    put_str(0,  HUD_ROW, "PWR", A_HUD);
    put_str(30, HUD_ROW, "SCORE", A_HUD);
    put_str(42, HUD_ROW, "ROWS", A_HUD);
    put_str(53, HUD_ROW, "WPN", A_HUD);
}

/* Last-drawn HUD values. Every field is diffed before it is repainted: the bar
 * alone is 20 cells, and repainting the whole HUD each tick costs more than the
 * simulation step it is reporting on. */
static unsigned int  hud_score = 0xFFFF, hud_rows = 0xFFFF, hud_energy = 0xFFFF;
static unsigned char hud_filled = 0xFF;
static unsigned char hud_oc = 0xFF, hud_pause = 0xFF;
static unsigned char hud_wpn = 0xFF, hud_wlev = 0xFF;

/* Padded to a common width so the field is fixed-size and the diff never has to
 * blank a leftover tail. Indexed by W_PLAIN/W_SPREAD/W_BEAM/W_HOMING. */
static const char *const wname[4] = { "PLAIN ", "SPREAD", "BEAM  ", "HOMING" };

/* A 20-cell bar plus the raw number: the bar for glanceable state mid-dodge, the
 * number for judging whether a node is worth passing up. */
static void draw_energy_bar(void) {
    unsigned char i, filled, a;

    filled = (unsigned char)(((unsigned long)energy * 20u) / ENERGY_MAX);
    a = (energy <= ENERGY_LOW) ? A_WARN : (energy <= (ENERGY_MAX / 2) ? A_MID : A_OK);

    if (filled != hud_filled) {
        hud_filled = filled;
        vaddr((unsigned int)HUD_ROW * SCR_W + 4);
        last_attr = 0xFF;
        for (i = 0; i < 20; i++) put_cell(i < filled ? G_BAR_FULL : G_BAR_EMPTY, a);
    }
    if (energy != hud_energy) {
        hud_energy = energy;
        put_num(25, HUD_ROW, energy, 4, a);
    }
}

static void draw_hud_live(void) {
    draw_energy_bar();
    if (score != hud_score) { hud_score = score; put_num(36, HUD_ROW, score, 5, A_TEXT); }
    if (rows  != hud_rows)  { hud_rows  = rows;  put_num(47, HUD_ROW, rows,  5, A_TEXT); }
    if (overclock != hud_oc) {
        hud_oc = overclock;
        put_str(68, HUD_ROW, overclock ? "[OC]" : "    ", A_WARN);
    }
    if (paused != hud_pause) {
        hud_pause = paused;
        put_str(73, HUD_ROW, paused ? "PAUSE" : "     ", A_WARN);
    }
    if (weapon != hud_wpn || wlevel != hud_wlev) {
        hud_wpn = weapon; hud_wlev = wlevel;
        put_str(57, HUD_ROW, wname[weapon], A_CRAFT);
        put_str(63, HUD_ROW, " Lv", A_HUD);
        put_num(66, HUD_ROW, wlevel, 1, A_CRAFT);
    }
}

/* ---- input ----
 * Non-blocking decode with a persistent ESC state machine: in a real-time loop
 * an arrow's three bytes (ESC [ A) can arrive across separate passes, so we
 * can't peek for them synchronously. A bare ESC never becomes an action --
 * every arrow starts with one, so it's ambiguous by construction. */
static unsigned char esc_state;

/* Consume bytes until a complete key is decoded or the buffer runs dry, so an
 * arrow's three bytes resolve within a SINGLE call. Returning -1 after each
 * partial byte (the earlier shape) meant an arrow needed three passes of the
 * main loop; once a frame got slow the keyboard buffer overflowed mid-sequence,
 * the state machine desynced, and arrows stopped decoding entirely.
 *
 * esc_state still persists across calls, because the bytes genuinely can arrive
 * split if the host delivers them late. */
static int getkey(void) {
    int c;
    for (;;) {
        c = INCH_NB();
        if (c < 0) return -1;                   /* buffer empty */
        switch (esc_state) {
            case 0:
                if (c == 0x1B) { esc_state = 1; break; }
                return c;
            case 1:
                esc_state = (c == '[') ? 2 : 0;
                break;
            default:
                esc_state = 0;
                switch (c) {
                    case 'A': return 'k';
                    case 'B': return 'j';
                    case 'C': return 'l';
                    case 'D': return 'h';
                }
                break;                          /* unknown sequence: drop it */
        }
    }
}

/* Returns 0 to quit.
 *
 * Only non-gameplay keys come through here now. Steering, firing and overclock
 * are sampled from the control port inside step_world() instead -- a keystroke
 * stream fundamentally cannot express "held", which is why they used to stutter
 * and why firing used to cancel movement. */
static unsigned char handle_key(int k) {
    switch (k) {
        case 'Q': case 'q':
            return 0;
        case 'P': case 'p':
            paused ^= 1;
            break;
        case '.':                       /* single-step while paused: frame debugging */
            if (paused) step_world();
            break;
        case '+': case '=':             /* smaller divisor = faster world */
            if (tickrate > TICK_MIN) tickrate--;
            break;
        case '-': case '_':
            if (tickrate < TICK_MAX) tickrate++;
            break;
        default:
            break;
    }
    return 1;
}

/* Drain everything already buffered, THEN block for one fresh key.
 *
 * This is why the end screen used to vanish before it could be read. It waited
 * with `while (INCH_NB() < 0) ;` -- but you die with a fistful of keys still in
 * the buffer, because you were holding them when it happened, so the wait was
 * already satisfied and the screen was acknowledged by a keystroke from before it
 * existed. Any screen a player is meant to READ has to discard the past first.
 *
 * `esc_state` is reset too: an arrow's three bytes can be split across the drain,
 * and a half-consumed escape sequence would swallow the next real key. */
static unsigned char wait_fresh_key(void) {
    int c;
    while (INCH_NB() >= 0) ;            /* discard whatever was already typed */
    esc_state = 0;
    for (;;) {
        c = getkey();
        if (c >= 0) return (unsigned char)c;
    }
}

/* Title screen. Waits for a deliberate key, and Q here quits without a run --
 * returns 0 to mean "quit". */
static unsigned char title_screen(void) {
    unsigned char k;

    vattr(A_TEXT); vaddr(0); vfill(' '); vcmd(VCMD_CLEAR);

    put_str(30, 4,  "K E R N E L", A_CRAFT);
    put_str(31, 5,  "P A N I C", A_WARN);
    put_str(24, 7,  "the conduit is corrupted -- run it", A_TEXT);

    put_str(26, 10, "arrows", A_HUD);   put_str(36, 10, "steer", A_TEXT);
    put_str(26, 11, "SPACE",  A_HUD);   put_str(36, 11, "fire", A_TEXT);
    put_str(26, 12, "SHIFT",  A_HUD);   put_str(36, 12, "overclock", A_TEXT);
    put_str(26, 13, "P",      A_HUD);   put_str(36, 13, "pause", A_TEXT);
    put_str(26, 14, "+ -",    A_HUD);   put_str(36, 14, "speed", A_TEXT);
    put_str(26, 15, "Q",      A_HUD);   put_str(36, 15, "quit", A_TEXT);

    put_str(24, 18, "S to start        Q to quit", A_BEVEL);

    for (;;) {
        k = wait_fresh_key();
        if (k == 'Q' || k == 'q') return 0;
        if (k == 'S' || k == 's' || k == ' ' || k == 13) return 1;
    }
}

/* End screen. Returns 1 to play again, 0 to quit. The real outcome tally with the
 * score table is step 7; this is the honest minimum -- it states the outcome, it
 * stays up until it is dismissed, and it says which key does what. */
static unsigned char game_over(void) {
    unsigned char k;

    /* Wipe to a plain screen -- which suits a panic, and means the panel is never
     * competing with terrain behind it. */
    vattr(A_TEXT); vaddr(0); vfill(' '); vcmd(VCMD_CLEAR);

    put_str(30, 9,  "*** KERNEL PANIC ***", A_WARN);
    put_str(28, 11, "ENERGY DEPLETED", A_TEXT);
    put_str(28, 12, "SCORE", A_HUD);
    put_num(38, 12, score, 5, A_TEXT);
    put_str(28, 13, "ROWS", A_HUD);
    put_num(38, 13, rows, 5, A_TEXT);
    put_str(27, 15, "R retry   Q quit", A_BEVEL);

    for (;;) {
        k = wait_fresh_key();
        if (k == 'Q' || k == 'q') return 0;
        if (k == 'R' || k == 'r' || k == ' ' || k == 13) return 1;
    }
}

/* One run, from a fresh conduit to death or Q. Returns 1 if the run ended in a
 * kernel panic (so the caller shows the end screen), 0 if the player quit out. */
static unsigned char play_run(void) {
    unsigned int  last, now;
    unsigned char catchup, hud_dirty = 1, r, quit;
    int k;

    /* Every run starts from the same state, because R on the end screen replays
     * without re-launching. Anything with an initialiser at file scope has to be
     * re-set here as well -- a static initialiser runs once per LOAD, not per run,
     * so a second run would otherwise inherit the first one's dead pools, spent
     * energy and deepened gun. */
    energy = ENERGY_MAX;
    score = 0; rows = 0; crashes = 0;
    dead = 0; paused = 0; overclock = 0; flash = 0; cooldown = 0;
    tickrate = TICK_DEFAULT;
    spawn_timer = SPAWN_MIN;
    weapon = W_PLAIN; wlevel = 1;
    head = 0;
    gen_cx = 19; gen_hw = HW_MAX; gen_target = HW_MAX; gen_gaunt = 0;
    isl_left = 0; isl_x = 0; isl_w = 0;
    for (r = 0; r < MAX_SHOTS; r++)   s_live[r] = 0;
    for (r = 0; r < MAX_ENEMIES; r++) e_type[r] = E_NONE;
    for (r = 0; r < MAX_PELLETS; r++) p_live[r] = 0;
    for (r = 0; r < MAX_FRAGS; r++)   f_live[r] = 0;
    /* Force every HUD field to repaint on the first frame of the run. */
    hud_score = 0xFFFF; hud_rows = 0xFFFF; hud_energy = 0xFFFF;
    hud_filled = 0xFF; hud_oc = 0xFF; hud_pause = 0xFF;
    hud_wpn = 0xFF; hud_wlev = 0xFF;

    vhidecur();
    vattr(A_TEXT); vaddr(0); vfill(' '); vcmd(VCMD_CLEAR);
    vscrollbot(BAND_BOT);               /* AFTER the clear -- a clear resets this */
    board_init();                       /* fixed trace routing for the whole run */

    /* Fill the playfield so the run opens inside a conduit rather than a void. */
    for (r = 0; r < PLAY_H; r++) {
        head = head ? (unsigned char)(head - 1) : (unsigned char)(PLAY_H - 1);
        rows++;
        gen_row();
    }
    for (r = 0; r < PLAY_H; r++) draw_row(r);

    craft_x = (unsigned char)((r_lx[slot(CRAFT_ROW)] + r_rx[slot(CRAFT_ROW)]) >> 1);

    draw_hud_static();
    draw_craft();

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
                catchup++;
                if (dead) break;
            }
            /* Still behind after the catch-up cap? Drop the backlog instead of
             * spinning forever trying to make it up. */
            if ((unsigned int)(now - last) >= tickrate) last = now;
            hud_dirty = 1;
        }

        if (dead) { draw_hud_live(); return 1; }

        if (hud_dirty) { draw_hud_live(); hud_dirty = 0; }

        /* Poll input every pass, not just on tick boundaries, so steering and
         * firing stay responsive independently of the world's scroll cadence.
         * Drain a few keys per pass so a burst of auto-repeat can't back up in
         * the kernel's keyboard buffer and start dropping bytes. */
        quit = 0;
        for (catchup = 0; catchup < 4; catchup++) {
            k = getkey();
            if (k < 0) break;
            if (!handle_key(k)) { quit = 1; break; }
            hud_dirty = 1;
        }
        if (quit) break;

        if (paused) last = jiffies();   /* don't bank a backlog while paused */
    }

    return 0;                           /* quit out mid-run */
}

void main(void) {
    rngv = rng_seed();
    if (rngv == 0) rngv = 0xACE1;       /* xorshift must never start at zero */

    vhidecur();
    while (title_screen()) {             /* Q on the title screen leaves */
        if (!play_run()) break;          /* quit out mid-run */
        if (!game_over()) break;         /* Q on the end screen leaves */
    }

    /* Hand the machine back the way we found it: full-screen scroll region. */
    vscrollbot(SCR_H - 1);
    QUITDOS();
}
