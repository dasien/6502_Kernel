/* ============================================================================
 * kpanic.c -- KERNEL PANIC: engine (build step 5).
 *
 * Working: the fixed-tick simulation loop paced off the kernel's 60 Hz jiffy
 * counter; the bounded scroll region with a pinned HUD; procedurally generated
 * meandering conduit terrain (gauntlets, islands, data nodes) in a per-row ring
 * buffer; a scrolling circuit-board backdrop; and the shared ENERGY pool.
 *
 * Controls come from the PIA's live key-state port ($FE0F), sampled once per
 * tick, NOT from the keystroke buffer. That stream carries no key-up, so it
 * cannot express "held": movement could only be inferred from OS auto-repeat
 * (~500ms stall) and firing cancelled steering outright, because the host only
 * repeats the most recently pressed key. Polling independent bits fixes both.
 *
 * The ENERGY pool is the game's whole point (see DESIGN.md): it drains as you
 * travel and refills only by flying over a data node. A node can be TAKEN for
 * energy or SHOT for score -- never both -- so every node is a bet on how long
 * you intend to live.
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
/* Firewall: the port's column, or 0 for an ordinary row. Column 0 is always outside the
 * channel, so 0 is a safe "none" and this needs no second array. r_fh is the port's
 * remaining health; when it reaches zero the whole row reverts to open lane, which is
 * what makes the barrier vanish the way River Raid's bridge does. */
static unsigned char r_fw[PLAY_H];
static unsigned char r_fh[PLAY_H];
static unsigned char fw_due;            /* set at a sector change; armed on the next row */
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

/* Generator knobs the sector sets. Constants until sectors needed to move them:
 * hw_floor is the tightest the channel may squeeze to, gaunt_ok gates the deliberate
 * squeezes entirely, and node_rate is the 1-in-N chance of a data node per row. */
static unsigned char hw_floor  = HW_MIN;
static unsigned char gaunt_ok  = 1;
static unsigned char node_rate = 90;

/* Is column x one of an island's two shore columns?
 *
 * An island is a piece of the SAME circuit board the banks outside the conduit are made
 * of -- so its interior is painted as board, not as wall. It used to be solid A_WALL edge
 * to edge, which at a pillar's 2-4 columns passed for terrain but at a big island's 5-8
 * read as a cyan blob dropped onto the lane. Only the shores stay cyan, which keeps the
 * "this edge kills you" cue the conduit walls establish. A 1- or 2-wide island is all
 * shore and so looks exactly as it did.
 *
 * Note the whole island is solid either way -- see blocked(). Green board already means
 * "land, do not fly here" everywhere else on screen, so this makes islands agree with
 * the rule rather than being an exception to it. */
static unsigned char isl_shore(unsigned char ix, unsigned char iw, unsigned char x) {
    return x == ix || x == (unsigned char)(ix + iw - 1);
}

/* Does a w-cell wide object starting at column x overlap the island in ring slot i?
 * Placement code for nodes and for enemies both need this, and both need it to be the
 * same test -- an object half-buried in a pillar looks like a bug in the renderer. */
static unsigned char isl_overlap(unsigned char i, unsigned char x, unsigned char w) {
    return r_iw[i] && x < (unsigned char)(r_ix[i] + r_iw[i]) &&
           (unsigned char)(x + w) > r_ix[i];
}

/* Generate one new row into the slot `head` currently points at. */
static void gen_row(void) {
    unsigned char lx, rx, span, nx;

    /* --- width: drift one step toward the target, re-picking the target now and
     * then. A "gauntlet" is just a hard, sustained low target. --- */
    if (gen_gaunt) {
        gen_gaunt--;
    } else if (gaunt_ok && rndn(70) == 0) {
        gen_target = (unsigned char)(hw_floor + rndn(3));       /* squeeze hard */
        gen_gaunt = 8 + rndn(10);
    } else if (rndn(22) == 0) {
        gen_target = (unsigned char)(hw_floor +
                                    rndn((unsigned char)(HW_MAX - hw_floor + 1)));
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
        if (gen_hw >= ISL_BIG_HW && rndn(ISL_BIG_ONE_IN) == 0) {
            /* A big one: wide and long enough that the two lanes either side are a
             * choice you commit to, not a gap you slip through. */
            isl_w = ISL_BIG_W + rndn(4);
            isl_left = ISL_BIG_ROWS + rndn(8);
        } else {
            /* Short: 1-3 rows. A middling pillar reads as a second wall rather than
             * an obstacle to weave around -- long enough to commit to but not long
             * enough to be worth committing to. Either short or properly big. */
            isl_w = 2 + rndn(3);
            isl_left = 1 + rndn(3);
        }
        span = (rx - lx - 1) - 6 - isl_w;       /* leave >=3 open each side */
        isl_x = lx + 4 + rndn(span);
    }
    /* Re-validate every row: the channel may have narrowed or shifted out from
     * under a still-running island, which would fuse it to a wall.
     *
     * On failure the island ENDS rather than skipping the row. A skipped row would
     * punch a hole through the middle of the pillar and then let it resume, which
     * reads as a rendering fault rather than as terrain -- and the longer the island,
     * the more rows it has to survive re-validation, so a big one would be visibly
     * perforated where a 2-row pillar mostly got away with it.
     *
     * The bounds enforce the same ">=3 open columns each side" the placement above
     * promises. They used to be symmetric at +/-2, which is right for the RIGHT lane
     * (rx - isl_x - isl_w >= 3) but one short on the LEFT, where the open run is
     * isl_x - lx - 1 and so needs isl_x > lx + 3. A 2-wide lane was reachable any time
     * the channel drifted under a live island -- measured at 608 rows per million, and
     * only rare before because an island lasted two rows. */
    if (isl_left && isl_x > lx + 3 && isl_x + isl_w < rx - 2) {
        r_ix[head] = isl_x;
        r_iw[head] = isl_w;
    } else {
        r_ix[head] = 0;
        r_iw[head] = 0;
        isl_left = 0;
    }

    /* --- firewall: one barrier row per sector, placed the moment the sector turns over.
     * It needs a channel wide enough that the port is reachable without scraping a
     * wall, so a tight gauntlet defers it to the next row. --- */
    r_fw[head] = 0;
    r_fh[head] = 0;
    if (fw_due && (unsigned char)(rx - lx) >= 6) {
        /* -3-FW_PORT_W so the LAST port cell still lands clear of the right wall. */
        r_fw[head] = (unsigned char)(lx + 2 +
                     rndn((unsigned char)(rx - lx - 3 - FW_PORT_W)));
        r_fh[head] = FW_PORT_HP;
        fw_due = 0;
    }

    /* --- data nodes: the only way to refill energy. Rare enough that you can't
     * take every one for granted, common enough to survive on. NODE_W cells wide,
     * placed at least one clear column inside each wall. --- */
    r_nx[head] = 0;
    if (rndn(node_rate) == 0) {
        span = rx - lx - NODE_W - 2;            /* valid start columns */
        if (span > 0) {
            nx = lx + 2 + rndn(span);
            /* reject if any of its cells would sit inside the island */
            if (!isl_overlap(head, nx, NODE_W))
                r_nx[head] = nx;
        }
    }
}

/* Is ring slot i a firewall row, and is column x part of its barrier? The port counts:
 * it is a target embedded in the barrier, not a hole to fly through. */
static unsigned char on_firewall(unsigned char i, unsigned char x) {
    return r_fw[i] && x > r_lx[i] && x < r_rx[i];
}

/* Does column x fall on the barrier's PORT -- the one part of it that takes damage?
 * FW_PORT_W wide, so it is a target you can line up on rather than a single cell. */
static unsigned char on_port(unsigned char i, unsigned char x) {
    return r_fw[i] && x >= r_fw[i] && x < (unsigned char)(r_fw[i] + FW_PORT_W);
}

/* Does column x fall on the node in ring slot i? */
static unsigned char on_node(unsigned char i, unsigned char x) {
    return r_nx[i] && x >= r_nx[i] && x < (unsigned char)(r_nx[i] + NODE_W);
}

/* Is column x blocked at screen row r (wall or island)? Nodes are not solid. */
static unsigned char blocked(unsigned char r, unsigned char x) {
    unsigned char i = slot(r);
    if (x <= r_lx[i] || x >= r_rx[i]) return 1;
    if (on_firewall(i, x)) return 1;    /* including the port: shoot it, do not fly it */
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

/* ============================================================================
 * Terrain appearance -- ONE definition, used by both paint paths
 *
 * There used to be two copies of the cell ladder: terrain_cell() for single-cell
 * restores and an inlined copy inside draw_row() for whole rows. Both carried comments
 * insisting they must agree. They did not: draw_row()'s copy had no firewall branch at
 * all, so a barrier was painted as ordinary lane -- solid, damaging, and completely
 * INVISIBLE. It only ever became visible where restore_cell() happened to repaint a cell
 * some object had vacated. A comment asserting an invariant is not an invariant.
 *
 * So the ladder now exists once, in row_cell(), and both callers go through it.
 *
 * The duplicate existed for a real reason, which is preserved: the board pattern needs a
 * `wy % 24` test, a 16-bit modulo that is a subroutine call under cc65, and paying it 80
 * times a row instead of once was measured to dominate the entire row paint. That phase
 * is now hoisted into two file-scope flags set by board_phase(). Keeping them out of the
 * argument list matters too -- cc65 pushes every argument onto the C stack, so a 4-arg
 * call per cell is meaningfully cheaper than a 6-arg one.
 * ==========================================================================*/

/* Board phase for one world row: which trace pattern it shows. Must be set before any
 * row_cell() call, and holds for all 80 cells of that row. */
static unsigned char ph_trace, ph_pad;

static void board_phase(unsigned int wy) {
    ph_trace = (unsigned char)((wy & 15) == 3);   /* a horizontal run every 16 rows */
    ph_pad   = (unsigned char)((wy % 24) == 0);   /* a solder pad every 24 */
}

/* Glyph of one circuit-board cell at the phase already set. Everything keys off the
 * WORLD row so the board scrolls with the conduit rather than standing still. */
static void board_glyph(unsigned char x, unsigned char *g) {
    if (ph_trace)          *g = board_col[x] ? G_VIA : G_TRACE_H;
    else if (board_col[x]) *g = ph_pad ? G_PAD : G_TRACE_V;
    else                   *g = ' ';
}

/* What does the cell at column x of ring slot i look like, ignoring moving objects?
 *
 * Precedence matters and is deliberate: a firewall outranks everything else inside the
 * channel, because when one is armed the whole span is barrier and neither an island nor
 * a node may punch a hole through it that the player could try to fly.
 *
 * Inside the conduit the lane is the very same board routing, just recessed into shadow,
 * so the channel reads as a trench cut into one continuous board with the traces lining
 * up across the wall. Two earlier attempts were both wrong instructively: a solid
 * dithered fill read unmistakably as water, and sparse dots on black read just as
 * unmistakably as a starfield. Reusing the board pattern avoids inventing a third visual
 * language and cannot fall out of alignment with the board by construction. */
static void row_cell(unsigned char i, unsigned char x,
                     unsigned char *g, unsigned char *a) {
    unsigned char lx = r_lx[i], rx = r_rx[i];

    if (x == lx || x == rx) {
        *g = G_WALL; *a = A_WALL;
    } else if (x == (unsigned char)(lx - 1) || x == (unsigned char)(rx + 1)) {
        *g = G_BEVEL; *a = A_BEVEL;
    } else if (x > lx && x < rx) {
        if (r_fw[i]) {                          /* barrier outranks island and node */
            if (on_port(i, x)) { *g = G_PORT; *a = A_PORT; }
            else              { *g = G_FIRE; *a = A_FIRE; }
        } else if (r_iw[i] && x >= r_ix[i] &&
                   x < (unsigned char)(r_ix[i] + r_iw[i])) {
            if (isl_shore(r_ix[i], r_iw[i], x)) { *g = G_WALL;  *a = A_WALL; }
            else                                { board_glyph(x, g); *a = A_BOARD; }
        } else if (on_node(i, x)) {
            *g = G_NODE; *a = A_NODE;
        } else {
            board_glyph(x, g); *a = A_RECESS;   /* recessed board in the channel */
        }
    } else {
        board_glyph(x, g); *a = A_BOARD;        /* board outside the conduit */
    }
}

/* Paint one cell of terrain (used to erase a moving object). */
static void restore_cell(unsigned char r, unsigned char x) {
    unsigned char g, a;
    board_phase(rows - r);
    row_cell(slot(r), x, &g, &a);
    vaddr(PROW(r) * SCR_W + x);
    last_attr = 0xFF;
    put_cell(g, a);
}

/* Draw screen row r as one contiguous 80-cell stream:
 *   [board] [bevel] [WALL] [ ... channel ... ] [WALL] [bevel] [board]
 * Each row is painted once when it enters and then just rides the hardware scroll down,
 * so terrain costs one row of writes per step, never a repaint. */
static void draw_row(unsigned char r) {
    unsigned char i = slot(r), x, g, a;

    board_phase(rows - r);
    vaddr(PROW(r) * SCR_W);
    last_attr = 0xFF;
    for (x = 0; x < PLAY_COLS; x++) {
        row_cell(i, x, &g, &a);
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
/* One row up from the band's bottom. The bottom row's overhang is what gets clipped
 * as the region slides, so a craft sitting there would be sliced off; row PLAY_LAST is
 * now the row terrain slides out through. */
#define CRAFT_ROW  (PLAY_H - 2)

/* Did an object sweep over the craft on its way from row `from` to row `to`?
 *
 * Not everything moves exactly one row per step. The world advances one, a daemon
 * adds a drift row on top of that, and a pellet adds one again -- so testing
 * `y == CRAFT_ROW` AFTER the move silently misses anything that stepped straight
 * over the craft, and a miss is indistinguishable from a collision bug. Anything
 * that can reach the craft is resolved across the span it travelled, not at its
 * landing row.
 *
 * Still required with overclock gone: a pellet moves two rows a step and a daemon
 * two, so the one-row cases are the exception rather than the rule. */
static unsigned char swept_craft(unsigned char from, unsigned char to,
                                 unsigned char x) {
    return (unsigned char)(from <= CRAFT_ROW && to >= CRAFT_ROW &&
                           x == craft_x);
}

/* The craft is SPRITE 0, not a cell.
 *
 * It used to be drawn into the character plane, and once the world started scrolling in
 * sub-cell steps that was untenable: the fine offset slides the whole plane, so the
 * craft slid down with the terrain and snapped back a full cell on every row -- the one
 * object on screen that is supposed to hold its row. A sprite is outside the region and
 * outside the offset, so it simply stays where it is put. */
static void draw_craft(void) {
    spr_sel(0);
    spr_x(craft_x);
    spr_y(CRAFT_ROW);
    if (flash) { spr_glyph(G_BLAST); spr_attr((unsigned char)(A_WARN | 0x80)); }
    else       { spr_glyph(G_CRAFT); spr_attr(A_CRAFT); }
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

/* How many free slots the pool has right now. */
static unsigned char shots_free(void) {
    unsigned char i, n = 0;
    for (i = 0; i < MAX_SHOTS; i++) if (!s_live[i]) n++;
    return n;
}

static void fire(void) {
    unsigned char want;

    if (cooldown) return;

    /* A volley is all or nothing: see MAX_SHOTS. Spawning a partial one would drop the
     * outermost shots and narrow the gun rather than slow it. */
    want = 1;
    if (weapon == W_SPREAD) want = (wlevel >= 3) ? 5 : 3;
    if (shots_free() < want) return;    /* no cooldown: retried next tick */

    cooldown = FIRE_COOLDOWN;

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
/* Short-lived debris, so a hit has a visible signature. A kill used to just stop
 * drawing the target, which is indistinguishable from the shot having gone straight
 * through -- the thing the player was already suspicious of. */
static unsigned char pop_x[MAX_DEBRIS], pop_y[MAX_DEBRIS], pop_t[MAX_DEBRIS];

/* Fade stages indexed by a cell's REMAINING ticks, so ageing is a table lookup rather
 * than a countdown branch. Slot 0 is never drawn (a dead cell) and exists only to make
 * the index the tick count directly. */
static const unsigned char pop_glyph[] = { 0, G_DUST, G_EMBER, G_BLAST };
static const unsigned char pop_attr[]  = { 0, A_TEXT, A_WARN,  A_SHOT  };

/* Where one hit throws its debris, as (dx, dy) from the impact cell. The first pair
 * covers the two cells the target itself occupied; the rest spray outward, symmetric
 * about the body's centre line. Fixed offsets rather than random ones: the table is
 * free to evaluate, and a repeated shape reads as one event coming apart where random
 * scatter reads as unrelated noise appearing nearby. */
static const signed char pop_dx[] = { 0, 1, -2,  2, -1, 3 };
static const signed char pop_dy[] = { 0, 0,  0,  0, -1, 1 };

/* Size these from their initialiser lists, not from the constants -- an array declared
 * [POP_CELLS] makes sizeof track POP_CELLS and the check can never fail. That exact
 * vacuous assert already got written once in VAULT and passed a deliberately broken
 * table. Verified here by temporarily bumping POP_CELLS, which does now fail. */
#pragma warn(const-comparison, push, off)
typedef char pop_dx_size_check[1 - 2 * (sizeof pop_dx != POP_CELLS)];
typedef char pop_dy_size_check[1 - 2 * (sizeof pop_dy != POP_CELLS)];
typedef char pop_glyph_size_check[1 - 2 * (sizeof pop_glyph != POP_TICKS + 1)];
typedef char pop_attr_size_check[1 - 2 * (sizeof pop_attr != POP_TICKS + 1)];
#pragma warn(const-comparison, pop)

/* Throw a scatter of debris centred on the cell that was hit. Cells falling outside
 * the playfield are dropped rather than clamped -- piling them on the boundary would
 * bunch the spray into a bar along the edge. */
static void pop_add(unsigned char x, unsigned char y) {
    unsigned char k, i;
    signed char px, py;

    for (k = 0; k < POP_CELLS; k++) {
        px = (signed char)((signed char)x + pop_dx[k]);
        py = (signed char)((signed char)y + pop_dy[k]);
        if (px < 0 || px >= PLAY_COLS) continue;
        if (py < 0 || py > PLAY_LAST)  continue;

        for (i = 0; i < MAX_DEBRIS; i++) if (!pop_t[i]) break;
        if (i == MAX_DEBRIS) return;            /* pool full: drop the rest */

        /* Outer cells start already part-faded, so the spray collapses inward as it
         * dies instead of every cell winking out on the same tick. */
        pop_t[i] = (unsigned char)(POP_TICKS - (k >> 1));
        pop_x[i] = (unsigned char)px;
        pop_y[i] = (unsigned char)py;
    }
}

/* Defined with the corruption pool below; declared here because shot collision
 * has to resolve against enemies and the firewall, and the weapon code comes first. */
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

    /* The firewall absorbs everything. Only the port takes damage, and when it goes the
     * whole barrier goes with it -- the row reverts to open lane, so the passage opens
     * exactly where the port was and the row has to be repainted once. */
    if (on_firewall(i, x)) {
        if (on_port(i, x) && --r_fh[i] == 0) {
            unsigned char q = (unsigned char)((r_rx[i] - r_lx[i]) >> 2);
            unsigned char bl = (unsigned char)(r_lx[i] + q);
            unsigned char br = (unsigned char)(r_rx[i] - q);
            r_fw[i] = 0;
            score += FW_SCORE;
            /* The barrier comes APART rather than blinking out of existence between one
             * frame and the next: debris at the port plus two points spread across the
             * span, so the break-up reads across the whole width it used to occupy.
             * Three scatters is most of the debris pool, and that is the right call --
             * nothing else on screen competes for attention at this moment. */
            pop_add(x, r);
            pop_add(bl, r);
            pop_add(br, r);
            draw_row(r);
        }
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
 * All screen-row motion is expressed as `WORLD_STEP + drift`. Drift 0 therefore
 * means "stays put in the conduit" and drift 1 means "closes on the player".
 *
 * WORLD_STEP used to be a parameter, because overclock ran the world two rows in one
 * step. With that gone it is exactly 1 by construction rather than by coincidence:
 * step_world() is invoked only when the fine-scroll accumulator crosses a full cell,
 * so one call is one row, always. Threading it as a variable that can hold one value
 * would be dead generality, so it is a constant that names the invariant. */
#define WORLD_STEP 1
static unsigned char e_type[MAX_ENEMIES];   /* E_NONE = free slot */
static unsigned char e_x[MAX_ENEMIES], e_y[MAX_ENEMIES];
/* The row this enemy started the tick on, and the column it descended in (before
 * a worm's weave). Needed to resolve shot collisions across the span it swept --
 * see shots_enemies_resolve(). */
static unsigned char e_from[MAX_ENEMIES], e_fx[MAX_ENEMIES];
static unsigned char e_hp[MAX_ENEMIES];
/* Ticks left showing a wounded enemy in the hit colour. A separate marker was wrong:
 * it stayed where the hit landed while the enemy carried on without it, so the feedback
 * pointed at empty space. Flashing the body itself travels with the body. */
static unsigned char e_flash[MAX_ENEMIES];
static unsigned char e_t[MAX_ENEMIES];      /* fire timer / weave phase */

static unsigned char p_live[MAX_PELLETS];
static unsigned char p_x[MAX_PELLETS], p_y[MAX_PELLETS];

static unsigned char spawn_timer = SPAWN_MIN;

/* Sub-cell offset in pixels, 0..CELL_H-1, and how many pixels a frame advances it.
 * speed_px is the PLAYER's throttle and nothing else writes it -- see sector_apply().
 *
 * An accumulator rather than a phase counter, because the old "frames per step" dial was
 * already at its floor of 1 with nowhere left to go -- and an accumulator frees the step
 * size from having to divide CELL_H, so speed is a smooth dial (2, 3, 4, 5...) instead of
 * the three usable values a divisor allowed.
 *
 * Below: the current sector's spawn interval floor (SPAWN_VAR rides on top), sentinel
 * fire interval, and enemy roster size. Everything else about a sector -- the terrain
 * generator, the weapons, the collision rules -- is identical, which is why sectors are
 * cheap; and every one of these dials is now a DENSITY rather than a tempo. */
static unsigned char fine_off;
static unsigned char speed_px = SPEED_DEFAULT;   /* the PLAYER's dial -- see sector_apply */
static unsigned char spawn_base = SPAWN_MIN;
static unsigned char fire_base  = SENTINEL_FIRE;
static unsigned char enemy_roster = 3;           /* how many enemy types may spawn */

static const char *const sector_name[NSECTORS] = { "KERNEL", "HEAP", "STACK", "I/O" };

/* SPEED IS NOT A DIFFICULTY DIAL. There used to be a sector_px[] = {2,3,4,5} here that
 * drove scroll rate off the sector, which is the one axis River Raid deliberately does
 * NOT escalate: its speedY is written only by the joystick (+2 up, -2 down, clamped) and
 * `level` never touches it. Scroll rate there is purely the player's throttle, and all
 * four of its real dials are density-shaped:
 *
 *   - enemy density up / fuel density down -- one roll, ~48%->88% enemy, ~24%->6% fuel
 *   - new enemy types unlocked with depth  -- planes only from level 3
 *   - minimum river width unlocked         -- valleyWidth 6 for levels 1-4, then 0
 *   - section geometry alternates          -- straight vs curved on level parity
 *
 * Worse, we ALSO gave the player up/down for speed, so two controls wrote speed_px and
 * the sector stomped whatever had been chosen -- first outright, then as a rising floor,
 * which is still the game moving the dial on its own. Speed now belongs to the player
 * alone and every sector dial below is a density.
 *
 * Enemy spawn interval, in rows. Pushed harder than the old {18,15,12,10} because the
 * 2.5x speed ramp used to carry a large share of late-sector pressure and no longer
 * contributes any. */
static const unsigned char sector_spawn[NSECTORS] = { 18, 14, 11,  8 };
/* Enemy roster size: sector 1 draws from {daemon, worm} only. The sentinel is the one
 * enemy that shoots back, and meeting one before you can hold a lane teaches nothing
 * except that you died -- the same reason planes are held back to level 3. */
static const unsigned char sector_roster[NSECTORS] = {  2,  3,  3,  3 };
/* Node scarcity, as a 1-in-N chance per row. The share of nodes you have to take just to
 * break even works out to ENERGY_DRAIN * N / ENERGY_NODE -- no tempo term, because drain
 * is charged per row -- so these are chosen straight off that: 31%, 40%, 49%, 57%.
 *
 * They were 90/110/125/145, i.e. 51%/63%/71%/83%, which left almost nothing over for
 * mistakes: simulated against a player model, even one claiming 90% of nodes and
 * crashing only once per 250 rows failed to reach the end of sector 4 in 44% of runs,
 * and a beginner failed in 99.7%. At these rates (with crashes repriced) that is 7% and
 * 80%. Four sectors should be an achievement, not a wall.
 *
 * Deliberately NO minimum spacing, despite the gap distribution being geometric and so
 * having a long tail. Forcing a node after a dry spell was measured and rejected: even a
 * floor as loose as 4N doubled a good player's distance and pushed perfect play toward
 * never dying, which would break "how far can you go" as a score. The scarcity cut
 * shrinks the lethal tail on its own. */
static const unsigned char sector_node[NSECTORS]  = { 55, 70, 85,100 };
/* Sentinel rate of fire: escalation on an axis that is not speed. */
static const unsigned char sector_fire[NSECTORS]  = { 14, 11,  9,  7 };


static unsigned char sector;            /* 0-based */
static unsigned int  sector_next;       /* row count at which the next one begins */
static unsigned int  fw_next;           /* row count at which the next firewall is armed */

/* NOTE: speed_px is deliberately absent. Nothing here may write it -- it is the player's
 * dial and the whole point of the density-only model above. */
static void sector_apply(void) {
    unsigned char i = (sector < NSECTORS) ? sector : (unsigned char)(NSECTORS - 1);
    spawn_base   = sector_spawn[i];
    node_rate    = sector_node[i];
    fire_base    = sector_fire[i];
    enemy_roster = sector_roster[i];
    /* The first sector teaches: a wider floor under the channel and no gauntlets. This
     * is our valleyWidth -- a minimum width that gets UNLOCKED with depth rather than a
     * channel that narrows progressively, which is how the original does it too. */
    hw_floor = (sector == 0) ? HW_MIN_EASY : HW_MIN;
    gaunt_ok = (sector != 0);
    /* Past the table, keep tightening the two dials that still have headroom rather than
     * stopping dead; the name repeats. Spawn interval has a floor of SPAWN_FLOOR so the
     * pool cannot be saturated to the point where spawns are silently dropped. */
    if (sector >= NSECTORS) {
        unsigned char extra = (unsigned char)(sector - NSECTORS + 1);
        spawn_base = (spawn_base > SPAWN_FLOOR + extra)
                   ? (unsigned char)(spawn_base - extra) : SPAWN_FLOOR;
        if ((unsigned int)node_rate + extra * 5u <= 200u) node_rate += extra * 5;
    }
}

/* Rows generated but not yet painted. draw_row() is 80 cells -- by far the most
 * expensive thing in a step -- and it used to run in the same frame that erases and
 * redraws every moving object, so the objects were missing for the ~10 ms it took and
 * the host painted them absent several times over. That is the "blinking".
 *
 * The new row is the hidden staging row, so nothing needs it this instant: defer it to
 * the next frame, which has nothing else to do. The boundary frame then only erases,
 * moves and redraws objects, and the window in which they do not exist is short.
 * Counts rather than flags because more than one row can still fall due between draws:
 * when the loop has fallen behind it runs step_world() up to MAX_CATCHUP times before
 * reaching a frame that paints. (It used to be counted because overclock scrolled twice
 * in a step; that reason is gone, the need is not.) */
static unsigned char rows_pending;

/* Push the current sub-cell offset to the chip. Paired with the row scroll inside
 * scroll_world() -- see the note there about why the two writes must be adjacent. */
static void fine_apply(void) {
    vfill(fine_off);
    vcmd(VCMD_FINEY);
}

/* Shots are sprites 1..MAX_SHOTS, and because a sprite is pixel-positioned they can be
 * drawn BETWEEN rows. Their logical position only changes at a step boundary, but the
 * eye sees FINE_STEPS frames per step -- so without interpolation a shot hops
 * SHOT_SPEED whole rows at once, which is what remained after the blink was fixed.
 *
 * The bias is centred on the shot's logical row rather than trailing from its previous
 * one: trailing is smooth too, but leaves the shot drawn up to SHOT_SPEED rows behind
 * where collision has already been resolved, so a kill would register while the shot was
 * still visibly short of the target. Centred halves that error in both directions.
 *
 * Called every frame, not just on step boundaries -- that is the whole point. A dead
 * slot must be switched OFF explicitly or its sprite lingers where the shot died. */
static void draw_shots(void) {
    unsigned char i;
    /* Centred on the shot's logical row: at fine_off 0 it is drawn SHOT_SPEED*CELL_H/2
     * below, at CELL_H-1 the same above, so the visual error stays under a row either
     * way instead of trailing the collision by a full SHOT_SPEED rows. */
    int bias = ((int)(CELL_H / 2) - (int)fine_off) * SHOT_SPEED;

    for (i = 0; i < MAX_SHOTS; i++) {
        spr_sel((unsigned char)(i + 1));
        if (s_live[i]) {
            int py = (int)s_y[i] * CELL_H + bias;
            if (py < 0) py = 0;         /* a shot near row 0 must not wrap negative */
            spr_x(s_x[i]);
            spr_y_px((unsigned int)py);
            spr_glyph(G_SHOT);
            spr_attr(A_SHOT);
            spr_on(1);
        } else {
            spr_on(0);
        }
    }
}



static unsigned char enemy_glyph(unsigned char t) {
    if (t == E_DAEMON)   return G_DAEMON;
    if (t == E_WORM)     return G_WORM;
    return G_SENTINEL;
}
static unsigned char enemy_attr(unsigned char t) {
    if (t == E_WORM)     return A_FOE2;
    if (t == E_SENTINEL) return A_FOE3;
    return A_FOE;                       /* the daemon keeps plain bright red */
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

/* Kept as decrement-and-check rather than collapsed to an unconditional kill: every
 * HP_* is 1 today, so the wounded branch below cannot currently be reached, and it is
 * therefore UNEXERCISED code rather than working code. It stays because a hardened
 * variant is the obvious way to escalate a late sector without touching tempo, and
 * this is the general form that would need. Anything relying on the flash path should
 * re-verify it -- it has not run since the HP tiers went away. */
static void enemy_damage(unsigned char idx) {
    if (--e_hp[idx] == 0) enemy_kill(idx);      /* a marker at the death site is right:
                                                 * there is no longer a body to flash */
    else e_flash[idx] = 2;                      /* wounded: flash the body, which moves */
}

/* Spawn one enemy at the top of the channel. Placed inside the walls with a
 * clear column either side, so it never appears already embedded in terrain. */
static void spawn_enemy(void) {
    unsigned char i, k, x, lx, rx, span, t;

    for (i = 0; i < MAX_ENEMIES; i++) if (!e_type[i]) break;
    if (i == MAX_ENEMIES) return;               /* pool full: skip this spawn */

    lx = r_lx[head];
    rx = r_rx[head];
    span = rx - lx - 2 - ENEMY_W;
    if (span == 0 || span > PLAY_COLS) return;  /* channel too tight to place one */

    /* Pick a column clear of any island. A big island covers a real share of a wide
     * channel and lasts long enough to see many spawns, so an unchecked placement would
     * regularly hatch an enemy already embedded in terrain -- rare enough to ignore with
     * 2-row pillars, not with these. A few tries then give up: skipping one spawn is
     * invisible, and there is no bound-tightening loop here that has to terminate. */
    for (k = 0; k < 4; k++) {
        x = (unsigned char)(lx + 2 + rndn(span));
        if (!isl_overlap(head, x, ENEMY_W)) break;
    }
    if (k == 4) return;                         /* no clear column found this tick */

    /* The roster grows with depth -- sector 1 has no sentinels. E_DAEMON..E_SENTINEL are
     * 1..3 in escalating order of nastiness, so a roster size doubles as "how far up that
     * order this sector reaches". */
    t = (unsigned char)(1 + rndn(enemy_roster));
    e_type[i] = t;
    e_x[i] = x;
    e_y[i] = 0;
    e_hp[i] = (t == E_DAEMON) ? HP_DAEMON : (t == E_WORM ? HP_WORM : HP_SENTINEL);
    e_t[i] = (t == E_SENTINEL) ? fire_base : (rnd16() & 1);
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

static void enemies_advance(void) {
    unsigned char i, ny, nx;

    for (i = 0; i < MAX_ENEMIES; i++) {
        if (!e_type[i]) continue;

        e_from[i] = e_y[i];
        e_fx[i]   = e_x[i];             /* the column it descends in, pre-weave */

        ny = e_y[i] + WORLD_STEP;
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
            else { pellet_spawn(e_x[i], e_y[i] + 1); e_t[i] = fire_base; }
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
 * fragment came FROM as well as the one it landed on.
 *
 * A fragment now moves exactly one row a step, so it can no longer skip the craft and
 * a landing-row test would be sufficient. The span test is kept anyway: because it
 * matches `from <= CRAFT_ROW && to >= CRAFT_ROW`, it also collects a fragment on the
 * row ABOVE the craft, which is a row of forgiveness on the one pickup in the game
 * you actively want. Deliberate, not vestigial. */
static void frags_step(void) {
    unsigned char f, ny;
    for (f = 0; f < MAX_FRAGS; f++) {
        if (!f_live[f]) continue;
        ny = f_y[f] + WORLD_STEP;

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

static void pellets_advance(void) {
    unsigned char i, ny;
    for (i = 0; i < MAX_PELLETS; i++) {
        if (!p_live[i]) continue;
        ny = p_y[i] + WORLD_STEP + 1;
        /* A pellet is the fastest thing coming at you -- two rows a step, the world's
         * one plus its own -- so it is the one that most needs resolving across its
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

        /* SHOT_SPEED + 1 tests, SHOT_SPEED movements: substep 0 tests the row the shot
         * ALREADY occupies, without moving it.
         *
         * That extra test is not belt-and-braces, it is the difference between the
         * firewall working and not. scroll_world() has already run by the time this does,
         * so terrain moved one row DOWN onto the shot. Relative to the world the shot
         * therefore crosses SHOT_SPEED + WORLD_STEP = 3 rows per step while only testing
         * the 2 it entered, and the row it skips is the one the terrain moved into. A
         * barrier and a shot could swap places without ever being compared -- so whether
         * a port took a hit depended on the parity of the closing gap, and on 1 of every
         * 3 approach alignments every shot passed clean through. Simulated: a craft
         * already sitting on the port column landed ZERO of its shots.
         *
         * Same tunnelling class as swept_craft() and shots_enemies_resolve(), but for
         * terrain that moves rather than objects that do -- and it applies to walls,
         * islands and nodes just as much as to the barrier. */
        for (step = 0; step <= SHOT_SPEED; step++) {
            if (step) {                                 /* substep 0 tests in place */
                if (s_y[i] == 0) { s_live[i] = 0; break; }   /* off the top */
                s_y[i]--;
            }
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

    /* The chip scroll and the sub-cell offset reset are two halves of ONE 2 px step:
     * the scroll moves content down a whole cell, the reset takes the offset from
     * CELL_H-FINE_PX back to zero, and the net is a single small advance. They must be
     * issued back to back, with nothing expensive in between.
     *
     * Getting this wrong is what made the whole playfield bounce. The reset used to
     * happen after step_world() returned -- so for the ~13 ms it takes to generate and
     * paint a new row, the content had already scrolled a full cell while the offset
     * still held its old value, leaving the world 16 px low for several host repaints
     * before it snapped back. Down, hold, up, once per row. Both writes now land in the
     * same host time slice, so no repaint can ever see them apart. */
    fine_off = 0;
    fine_apply();

    head = head ? (unsigned char)(head - 1) : (unsigned char)(PLAY_H - 1);
    rows++;
    gen_row();
    if (rows_pending < PLAY_H) rows_pending++;   /* painted on the next frame */

    /* Firewall, on its own cadence -- see FW_ROWS. Arms a flag rather than writing the
     * barrier here so that it is built by gen_row() in the normal row-building path,
     * which owns every other feature of a row. gen_row()'s width guard is then a safety
     * net rather than a real deferral: the narrowest channel HW_MIN allows spans 8, and
     * the guard needs 6, so in practice the barrier always lands on the very next row.
     * The check stays because the guard and HW_MIN are free to move independently. */
    if (rows >= fw_next) {
        fw_next += FW_ROWS;
        fw_due = 1;
    }

    /* Sector change. The banner is written straight into the new row and then rides the
     * scroll down the screen with everything else -- no timer, no overlay, and it reads
     * as a message drifting past rather than a modal interruption. */
    if (rows >= sector_next) {
        const char *nm;
        unsigned char lx, rx, w = 0, at;
        sector++;
        sector_next += SECTOR_ROWS;
        sector_apply();
        nm = sector_name[(sector < NSECTORS) ? sector : (NSECTORS - 1)];
        lx = r_lx[head]; rx = r_rx[head];
        while (nm[w]) w++;
        /* Only if the channel is wide enough to hold it clear of both walls. */
        if ((unsigned char)(rx - lx) > (unsigned char)(w + 2)) {
            at = (unsigned char)(lx + ((rx - lx - w) >> 1));
            put_str(at, 0, nm, A_CRAFT);
        }
    }
}

/* One fixed simulation step: the single place anything moves, so world speed is
 * purely the tick divisor -- no per-object fractional speeds. */
static void step_world(void) {
    unsigned char i, ks;

    /* Erase every moving object BEFORE the scroll, or the hardware shift drags
     * their glyphs down the screen as a trail of ghosts. */
    /* Neither the craft nor the shots are erased: both are sprites now and never
     * touch the plane. That is what stops them blinking -- a cell-plane object is
     * absent from the screen for the whole gap between its erase and its redraw, and
     * the host repaints several times inside that gap. */
    for (i = 0; i < MAX_ENEMIES; i++)
        if (e_type[i]) {
            restore_cell(e_y[i], e_x[i]);
            restore_cell(e_y[i], (unsigned char)(e_x[i] + 1));
        }
    /* One cell each now: the scatter table's first two entries already cover the two
     * cells a target body occupied, so restoring x+1 as well would clean a column of
     * lane the debris never wrote to. */
    for (i = 0; i < MAX_DEBRIS; i++)
        if (pop_t[i]) restore_cell(pop_y[i], pop_x[i]);
    for (i = 0; i < MAX_PELLETS; i++)
        if (p_live[i]) restore_cell(p_y[i], p_x[i]);
    for (i = 0; i < MAX_FRAGS; i++)
        if (f_live[i]) restore_cell(f_y[i], f_x[i]);

    /* Sample the live control port ONCE per tick and act on every bit that is
     * set. Movement is therefore exactly as smooth as the tick rate, and steering
     * while firing costs nothing: the bits are independent, so there is no
     * "most recent key wins" behaviour to fight. */
    ks = keystate();

    scroll_world();

    if (flash) flash--;
    if (cooldown) cooldown--;
    for (i = 0; i < MAX_DEBRIS; i++) if (pop_t[i]) pop_t[i]--;
    for (i = 0; i < MAX_ENEMIES; i++) if (e_flash[i]) e_flash[i]--;

    if (ks & KS_LEFT)  craft_x -= MOVE_PER_TICK;
    if (ks & KS_RIGHT) craft_x += MOVE_PER_TICK;
    if (ks & KS_FIRE)  fire();          /* cooldown paces it; holding is fine */

    shots_advance();
    enemies_advance();
    shots_enemies_resolve();    /* AFTER both have moved -- see the note there */
    pellets_advance();
    frags_step();

    if (--spawn_timer == 0) {
        spawn_enemy();
        spawn_timer = spawn_base + rndn(SPAWN_VAR);
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

    /* A firewall hit is charged before the generic crash so it costs its own, heavier
     * amount -- and the barrier is cleared, so you are through rather than grinding
     * against it while it scrolls past. */
    i = slot(CRAFT_ROW);
    if (on_firewall(i, craft_x)) {
        r_fw[i] = 0;
        draw_row(CRAFT_ROW);
        flash = 3;
        energy_spend(FW_CRASH);
    } else if (blocked(CRAFT_ROW, craft_x)) {
        crash();
    }

    energy_spend(ENERGY_DRAIN);

    /* Redraw order is deliberate: corruption, then pellets, then your shots, then
     * the craft. Later writes win, so the things you most need to see never end
     * up hidden under something else sharing a cell. */
    /* Debris first, so a live object drawn over a cell still wins it. Glyph and colour
     * both come from the remaining-ticks tables, which is what makes the spray thin out
     * rather than switch off. */
    for (i = 0; i < MAX_DEBRIS; i++)
        if (pop_t[i]) {
            vaddr(PROW(pop_y[i]) * SCR_W + pop_x[i]);
            last_attr = 0xFF;
            put_cell(pop_glyph[pop_t[i]], pop_attr[pop_t[i]]);
        }
    for (i = 0; i < MAX_ENEMIES; i++)
        if (e_type[i]) {
            vaddr(PROW(e_y[i]) * SCR_W + e_x[i]);
            last_attr = 0xFF;
            {
                unsigned char ea = e_flash[i] ? A_SHOT : enemy_attr(e_type[i]);
                put_cell(enemy_glyph(e_type[i]), ea);
                put_cell(enemy_glyph(e_type[i]), ea);
            }
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
    draw_shots();
    draw_craft();                       /* a sprite: cheap, and never leaves a hole */
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
    put_str(42, HUD_ROW, "DIST", A_HUD);
    put_str(53, HUD_ROW, "WPN", A_HUD);
}

/* Last-drawn HUD values. Every field is diffed before it is repainted: the bar
 * alone is 20 cells, and repainting the whole HUD each tick costs more than the
 * simulation step it is reporting on. */
static unsigned int  hud_score = 0xFFFF, hud_rows = 0xFFFF, hud_energy = 0xFFFF;
static unsigned char hud_filled = 0xFF;
static unsigned char hud_pause = 0xFF;
static unsigned char hud_wpn = 0xFF, hud_wlev = 0xFF;

static unsigned char hud_sector = 0xFF;

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
    /* Sector number. This replaced a steps-per-second diagnostic, which had served its
     * purpose -- it confirmed the loop holds 60+ and is not the source of any judder. */
    if (sector != hud_sector) {
        hud_sector = sector;
        put_str(78, HUD_ROW, "S", A_HUD);
        put_num(79, HUD_ROW, (unsigned int)(sector + 1), 1, A_CRAFT);
    }
    if (score != hud_score) { hud_score = score; put_num(36, HUD_ROW, score, 5, A_TEXT); }
    if (rows  != hud_rows)  { hud_rows  = rows;  put_num(47, HUD_ROW, rows,  5, A_TEXT); }
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
 * Only non-gameplay keys come through here now. Steering and firing are
 * sampled from the control port inside step_world() instead -- a keystroke
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
        /* Speed on the UP/DOWN arrows. +/- needed a third hand: steering is already on
         * the arrows and firing on the space bar, so reaching for the number row meant
         * letting go of something. getkey() decodes the arrow escape sequences to
         * h/j/k/l; left and right are ignored here because steering comes from the
         * control port, not the keystroke stream. */
        case 'k':                       /* up: more pixels per frame = faster world */
        case '+': case '=':
            if (speed_px < SPEED_MAX) speed_px++;
            break;
        case 'j':                       /* down: slower */
        case '-': case '_':
            if (speed_px > SPEED_MIN) speed_px--;
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

    /* "arrows" said steer while a later line said up/down was speed -- two entries
     * claiming the same keys. Name the halves separately. */
    put_str(26, 10, "left/right", A_HUD);   put_str(38, 10, "steer", A_TEXT);
    put_str(26, 11, "SPACE",      A_HUD);   put_str(38, 11, "fire", A_TEXT);
    put_str(26, 12, "up/down",    A_HUD);   put_str(38, 12, "speed", A_TEXT);
    put_str(26, 13, "P",          A_HUD);   put_str(38, 13, "pause", A_TEXT);
    put_str(26, 14, "Q",          A_HUD);   put_str(38, 14, "quit", A_TEXT);

    put_str(24, 20, "S to start        Q to quit", A_BEVEL);

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
    put_str(28, 10, "SECTOR", A_HUD);
    put_str(38, 10, sector_name[(sector < NSECTORS) ? sector : (NSECTORS - 1)], A_CRAFT);
    put_str(28, 12, "SCORE", A_HUD);
    put_num(38, 12, score, 5, A_TEXT);
    put_str(28, 13, "DIST", A_HUD);
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
    dead = 0; paused = 0; flash = 0; cooldown = 0;
    tickrate = TICK_DEFAULT;
    spawn_timer = SPAWN_MIN;
    fine_off = 0;
    fw_due = 0;                     /* fw_next is set with sector_next, further down */
    weapon = W_PLAIN; wlevel = 1;
    head = 0;
    gen_cx = 19; gen_hw = HW_MAX; gen_target = HW_MAX; gen_gaunt = 0;
    isl_left = 0; isl_x = 0; isl_w = 0;
    for (r = 0; r < MAX_SHOTS; r++)   s_live[r] = 0;
    for (r = 0; r < MAX_ENEMIES; r++) { e_type[r] = E_NONE; e_flash[r] = 0; }
    for (r = 0; r < MAX_PELLETS; r++) p_live[r] = 0;
    for (r = 0; r < MAX_FRAGS; r++)   f_live[r] = 0;
    for (r = 0; r < MAX_DEBRIS; r++)  pop_t[r] = 0;
    /* Force every HUD field to repaint on the first frame of the run. */
    hud_score = 0xFFFF; hud_rows = 0xFFFF; hud_energy = 0xFFFF;
    hud_filled = 0xFF; hud_pause = 0xFF; hud_sector = 0xFF;
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

    sector = 0;
    sector_next = SECTOR_ROWS;
    fw_next = FW_ROWS;
    /* Explicit now that sector_apply() no longer touches it: a second run in the same
     * LOAD would otherwise open at whatever throttle the last one ended on. */
    speed_px = SPEED_DEFAULT;
    sector_apply();
    fine_off = 0;
    fine_apply();                       /* AFTER the clear -- a clear turns fine off */

    draw_hud_static();
    draw_craft();
    spr_sel(0);
    spr_on(1);                          /* AFTER a position is set, so it never
                                         * flashes on at a stale coordinate */

    last = jiffies();

    for (;;) {
        /* --- fixed-timestep accumulator, all integer. Unsigned subtraction
         * makes the counter's ~18-minute wrap harmless. --- */
        now = jiffies();
        if (!paused && (unsigned int)(now - last) >= tickrate) {
            catchup = 0;
            while ((unsigned int)(now - last) >= tickrate && catchup < MAX_CATCHUP) {
                /* Advance the sub-cell offset; a full cell's worth of it is one row
                 * of world, at which point the chip does the real scroll and the
                 * simulation takes its step. */
                fine_off = (unsigned char)(fine_off + speed_px);
                if (fine_off >= CELL_H) {
                    /* step_world() resets the offset itself, right next to the chip
                     * scroll -- see scroll_world(). Applying it again here would be
                     * harmless, but doing it ONLY there keeps the invariant obvious. */
                    step_world();
                } else {
                    fine_apply();
                    /* Shots slide between rows, so their sprites are repositioned every
                     * frame rather than once a step -- see draw_shots(). */
                    draw_shots();
                    /* The expensive terrain paint, on a frame that is otherwise idle.
                     * Newest row last, so the ring's slot order is respected. */
                    while (rows_pending) {
                        rows_pending--;
                        draw_row(rows_pending);
                    }
                }
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
    unsigned char i;

    rngv = rng_seed();
    if (rngv == 0) rngv = 0xACE1;       /* xorshift must never start at zero */

    vhidecur();
    while (title_screen()) {             /* Q on the title screen leaves */
        if (!play_run()) break;          /* quit out mid-run */
        if (!game_over()) break;         /* Q on the end screen leaves */
    }

    /* Hand the machine back the way we found it: no sprite, full-screen scroll
     * region. A clear would do it too, but being explicit costs nothing. */
    for (i = 0; i <= MAX_SHOTS; i++) { spr_sel(i); spr_on(0); }
    vscrollbot(SCR_H - 1);
    QUITDOS();
}
