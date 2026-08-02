/* ============================================================================
 * kpanic.c -- KERNEL PANIC: engine (build step 4).
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
 * Deliberately NOT here yet: power-ups (step 5), bosses (6), juice/scoring
 * polish (7). The game-over screen is a placeholder.
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
static unsigned char board_col[SCR_W];

static void board_init(void) {
    unsigned char x = 1;
    while (x < SCR_W) {
        board_col[x] = 1;
        if (rndn(4) == 0 && x + 1 < SCR_W) { board_col[x + 1] = 1; x++; }
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
    vaddr((unsigned int)r * SCR_W + x);
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

    vaddr((unsigned int)r * SCR_W);
    last_attr = 0xFF;
    for (x = 0; x < SCR_W; x++) {
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

#define CRAFT_ROW  (PLAY_BOT - 1)

static void draw_craft(void) {
    vaddr((unsigned int)CRAFT_ROW * SCR_W + craft_x);
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
}

/* ---- weapon ---- */
static void fire(void) {
    unsigned char i;
    if (cooldown) return;
    for (i = 0; i < MAX_SHOTS; i++) {
        if (!s_live[i]) {
            s_live[i] = 1;
            s_x[i] = craft_x;
            s_y[i] = CRAFT_ROW - 1;
            cooldown = overclock ? FIRE_COOLDOWN_OC : FIRE_COOLDOWN;
            return;
        }
    }
}

/* Resolve a shot arriving at one cell. Returns 1 if the shot is consumed.
 * Checked per single-cell substep rather than at the destination, so a fast shot
 * can't tunnel through a node or a wall. */
/* Defined with the corruption pool below; declared here because shot collision
 * has to resolve against enemies and the weapon code comes first. */
static unsigned char enemy_at(unsigned char r, unsigned char x);
static void          enemy_damage(unsigned char idx);

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
    return blocked(r, x);
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
static unsigned char e_hp[MAX_ENEMIES];
static unsigned char e_t[MAX_ENEMIES];      /* fire timer / weave phase */

static unsigned char p_live[MAX_PELLETS];
static unsigned char p_x[MAX_PELLETS], p_y[MAX_PELLETS];

static unsigned char spawn_timer = SPAWN_MIN;

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
        if (e_type[i] && e_y[i] == r && e_x[i] == x) return i + 1;
    return 0;
}

static void enemy_kill(unsigned char i) {
    if (e_type[i] == E_DAEMON)        score += SCORE_DAEMON;
    else if (e_type[i] == E_WORM)     score += SCORE_WORM;
    else                              score += SCORE_SENTINEL;
    e_type[i] = E_NONE;
    restore_cell(e_y[i], e_x[i]);
}

static void enemy_damage(unsigned char idx) {
    if (--e_hp[idx] == 0) enemy_kill(idx);
}

/* Spawn one enemy at the top of the channel. Placed inside the walls with a
 * clear column either side, so it never appears already embedded in terrain. */
static void spawn_enemy(void) {
    unsigned char i, lx, rx, span, t;

    for (i = 0; i < MAX_ENEMIES; i++) if (!e_type[i]) break;
    if (i == MAX_ENEMIES) return;               /* pool full: skip this spawn */

    lx = r_lx[head];
    rx = r_rx[head];
    span = rx - lx - 3;
    if (span == 0) return;                      /* channel too tight to place one */

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

        ny = e_y[i] + scrolled;
        if (e_type[i] == E_DAEMON) ny++;        /* closes faster than the world */

        if (ny > PLAY_BOT) { e_type[i] = E_NONE; continue; }   /* off the bottom */
        e_y[i] = ny;

        if (e_type[i] == E_WORM) {
            /* Weave one column at a time, reversing at the walls rather than
             * grinding along them. */
            nx = e_t[i] ? e_x[i] + 1 : e_x[i] - 1;
            if (blocked(e_y[i], nx)) e_t[i] ^= 1;
            else                     e_x[i] = nx;
        } else if (e_type[i] == E_SENTINEL) {
            if (e_t[i]) e_t[i]--;
            else { pellet_spawn(e_x[i], e_y[i] + 1); e_t[i] = SENTINEL_FIRE; }
        }

        /* Crushed by a narrowing channel. Corruption is not privileged over the
         * conduit -- if the walls close on it, it dies like anything else. */
        if (blocked(e_y[i], e_x[i])) e_type[i] = E_NONE;
    }
}

static void pellets_advance(unsigned char scrolled) {
    unsigned char i, ny;
    for (i = 0; i < MAX_PELLETS; i++) {
        if (!p_live[i]) continue;
        ny = p_y[i] + scrolled + 1;
        if (ny > PLAY_BOT || blocked(ny, p_x[i])) { p_live[i] = 0; continue; }
        p_y[i] = ny;
    }
}

static void shots_advance(void) {
    unsigned char i, step;
    for (i = 0; i < MAX_SHOTS; i++) {
        if (!s_live[i]) continue;
        for (step = 0; step < SHOT_SPEED; step++) {
            if (s_y[i] == 0) { s_live[i] = 0; break; }   /* off the top */
            s_y[i]--;
            if (shot_hits(s_y[i], s_x[i])) { s_live[i] = 0; break; }
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
        if (e_type[i]) restore_cell(e_y[i], e_x[i]);
    for (i = 0; i < MAX_PELLETS; i++)
        if (p_live[i]) restore_cell(p_y[i], p_x[i]);

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

    if (ks & KS_LEFT)  craft_x -= MOVE_PER_TICK;
    if (ks & KS_RIGHT) craft_x += MOVE_PER_TICK;
    if (ks & KS_FIRE)  fire();          /* cooldown paces it; holding is fine */

    shots_advance();
    enemies_advance(scrolled);
    pellets_advance(scrolled);

    if (--spawn_timer == 0) {
        spawn_enemy();
        spawn_timer = SPAWN_MIN + rndn(SPAWN_VAR);
    }

    /* Corruption you fly into: costs energy and dies with you passing through --
     * there is no bouncing off, so a missed dodge is always paid for. */
    i = enemy_at(CRAFT_ROW, craft_x);
    if (i) {
        e_type[i - 1] = E_NONE;
        restore_cell(e_y[i - 1], e_x[i - 1]);
        flash = 3;
        energy_spend(ENERGY_HIT);
    }
    for (i = 0; i < MAX_PELLETS; i++) {
        if (p_live[i] && p_y[i] == CRAFT_ROW && p_x[i] == craft_x) {
            p_live[i] = 0;
            restore_cell(p_y[i], p_x[i]);
            flash = 3;
            energy_spend(ENERGY_PELLET);
        }
    }

    /* Take a node by flying over it: refill, and forfeit the score you'd have
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
    for (i = 0; i < MAX_ENEMIES; i++)
        if (e_type[i]) {
            vaddr((unsigned int)e_y[i] * SCR_W + e_x[i]);
            last_attr = 0xFF;
            put_cell(enemy_glyph(e_type[i]), enemy_attr(e_type[i]));
        }
    for (i = 0; i < MAX_PELLETS; i++)
        if (p_live[i]) {
            vaddr((unsigned int)p_y[i] * SCR_W + p_x[i]);
            last_attr = 0xFF;
            put_cell(G_PELLET, A_FOE);
        }
    for (i = 0; i < MAX_SHOTS; i++)
        if (s_live[i]) {
            vaddr((unsigned int)s_y[i] * SCR_W + s_x[i]);
            last_attr = 0xFF;
            put_cell(G_SHOT, A_SHOT);
        }
    draw_craft();
}

/* ---- HUD (rows 22..24, pinned below the scroll region) ---- */
static void draw_hud_static(void) {
    unsigned char i;

    vaddr((unsigned int)HUD_ROW * SCR_W);
    last_attr = 0xFF;
    for (i = 0; i < SCR_W; i++) put_cell(G_HBAR, A_HUD);
    put_str(2, HUD_ROW, " KERNEL PANIC  v1.0  corruption ", A_CRAFT);

    put_str(2,  HUD_ROW + 1, "PWR", A_HUD);
    put_str(34, HUD_ROW + 1, "SCORE:", A_HUD);
    put_str(48, HUD_ROW + 1, "ROWS:", A_HUD);
    put_str(61, HUD_ROW + 1, "ACT:", A_HUD);
    put_str(67, HUD_ROW + 1, "/s", A_HUD);

    put_str(2, HUD_ROW + 2,
            "arrows steer   SPACE fire   SHIFT overclock   P pause   . step   +/- speed   Q quit",
            A_TEXT);
}

/* Last-drawn HUD values. Every field is diffed before it is repainted: the bar
 * alone is 24 cells, and repainting the whole HUD each tick costs more than the
 * simulation step it is reporting on. The bar in particular only changes once
 * per 50 energy, i.e. roughly every 25 ticks. */
static unsigned int  hud_score = 0xFFFF, hud_rows = 0xFFFF, hud_energy = 0xFFFF;
static unsigned char hud_filled = 0xFF, hud_meas = 0xFF;
static unsigned char hud_oc = 0xFF, hud_pause = 0xFF;

/* A 20-cell bar plus the raw number: the bar for glanceable state mid-dodge, the
 * number for judging whether a node is worth passing up. */
static void draw_energy_bar(void) {
    unsigned char i, filled, a;

    filled = (unsigned char)(((unsigned long)energy * 20u) / ENERGY_MAX);
    a = (energy <= ENERGY_LOW) ? A_WARN : (energy <= (ENERGY_MAX / 2) ? A_MID : A_OK);

    if (filled != hud_filled) {
        hud_filled = filled;
        vaddr((unsigned int)(HUD_ROW + 1) * SCR_W + 6);
        last_attr = 0xFF;
        for (i = 0; i < 20; i++) put_cell(i < filled ? G_BAR_FULL : G_BAR_EMPTY, a);
    }
    if (energy != hud_energy) {
        hud_energy = energy;
        put_num(27, HUD_ROW + 1, energy, 4, a);
    }
}

static void draw_hud_live(unsigned char measured) {
    draw_energy_bar();
    if (score != hud_score) { hud_score = score; put_num(40, HUD_ROW + 1, score, 5, A_TEXT); }
    if (rows  != hud_rows)  { hud_rows  = rows;  put_num(53, HUD_ROW + 1, rows,  5, A_TEXT); }
    if (measured != hud_meas) {
        hud_meas = measured;
        put_num(65, HUD_ROW + 1, measured, 2,
                (measured + 1u >= 60u / tickrate) ? A_TEXT : A_WARN);
    }
    if (overclock != hud_oc) {
        hud_oc = overclock;
        put_str(70, HUD_ROW + 1, overclock ? "[OC]" : "    ", A_WARN);
    }
    if (paused != hud_pause) {
        hud_pause = paused;
        put_str(75, HUD_ROW + 1, paused ? "PAUSE" : "     ", A_WARN);
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

/* Placeholder end screen; the real outcome tally is step 7. */
static void game_over(void) {
    unsigned char r, c;
    for (r = 8; r <= 16; r++) {
        vaddr((unsigned int)r * SCR_W + 24);
        last_attr = 0xFF;
        for (c = 0; c < 32; c++) put_cell(' ', A_TEXT);
    }
    put_str(30, 9,  "*** KERNEL PANIC ***", A_WARN);
    put_str(28, 11, "ENERGY DEPLETED", A_TEXT);
    put_str(28, 12, "SCORE", A_HUD);
    put_num(38, 12, score, 5, A_TEXT);
    put_str(28, 13, "ROWS", A_HUD);
    put_num(38, 13, rows, 5, A_TEXT);
    put_str(26, 15, "press any key", A_BEVEL);
    while (INCH_NB() < 0) ;             /* wait for acknowledgement */
}

void main(void) {
    unsigned int  last, now;
    unsigned char catchup, sec, persec = 0, measured = 0, hud_dirty = 1, r, quit;
    int k;

    rngv = rng_seed();
    if (rngv == 0) rngv = 0xACE1;       /* xorshift must never start at zero */

    vhidecur();
    vattr(A_TEXT); vaddr(0); vfill(' '); vcmd(VCMD_CLEAR);
    vscrollbot(PLAY_BOT);               /* AFTER the clear -- a clear resets this */
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
                if (dead) break;
            }
            /* Still behind after the catch-up cap? Drop the backlog instead of
             * spinning forever trying to make it up. */
            if ((unsigned int)(now - last) >= tickrate) last = now;
            hud_dirty = 1;
        }

        if (dead) { draw_hud_live(measured); game_over(); break; }

        /* Measure real steps-per-second off the RTC -- the readout that proves
         * the loop is actually pacing at the target rate. */
        if (rtc_sec() != sec) {
            sec = rtc_sec();
            measured = persec;
            persec = 0;
            hud_dirty = 1;
        }

        if (hud_dirty) { draw_hud_live(measured); hud_dirty = 0; }

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

    QUITDOS();
}
