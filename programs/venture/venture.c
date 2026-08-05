/*
 * VENTURE for MFC -- a port of Exidy's Venture (1981).
 *
 * Build steps 1-5 of DESIGN.md: one playable room. What runs here is the whole
 * core loop -- fixed-tick pacing off the 60 Hz jiffy counter, eight-way movement
 * from the live control port, one arrow in flight, serpents that hunt you, bodies
 * that stay lethal, and the original's scoring rule. The dungeon map, the
 * Hallmonsters, the remaining rooms and the level loop come next.
 *
 * The point of stopping here is that this is where the design either feels like
 * Venture or does not, and that is worth judging before building eleven more
 * rooms on top of it.
 */

#include "venture.h"

/* ---- room ---------------------------------------------------------------
 * The room is held as a tile grid in RAM, not read back from the screen: the
 * screen is behind a register port with no random-access read, and we need to
 * know what a cell *is* (floor, wall, corpse) separately from what it looks
 * like. Corpses and the taken treasure are recorded here.
 *
 * The layout is a string per row so it can be read and edited as a picture.
 * Twelve rooms of this will want packing -- 44x15 is 660 bytes each -- but at one
 * room the clarity is worth more than the bytes. '#' wall, '.' floor, '*'
 * treasure, 'm' a monster spawn (becomes floor once the monster is placed). */
static const char *const room_1[ROOM_H] = {
    "############################################",
    "#..........................................#",
    "#..####............m...............####....#",
    "#..#..............................#...#....#",
    "#..#....####.................####.#...#....#",
    "#.......#..#.................#..#......#####",
    "#.......#..#......m..........#..#..........#",
    "#.......####.................####..........#",
    "#..........................................#",
    "#####..........####...####.............#####",
    "#..................#...#.......m...........#",
    "#..####............#...#...................#",
    "#..#..*............#####...................#",
    "#..#.......................................#",
    "############################################",
};

static unsigned char grid[ROOM_H][ROOM_W];

/* ---- entities ---------------------------------------------------------- */
static unsigned char wx, wy;             /* Winky, in room coordinates */
static signed char   face_dx, face_dy;   /* last direction held; arrows use it */
static unsigned char anim;               /* toggles the two Winky frames */

static unsigned char m_live[MAX_MON];
static unsigned char m_x[MAX_MON], m_y[MAX_MON];
static unsigned char mon_count;

static unsigned char a_live;             /* one arrow in flight, as the original */
static unsigned char a_x, a_y;
static signed char   a_dx, a_dy;

/* ---- game state -------------------------------------------------------- */
static unsigned int  score;
static unsigned char lives = LIVES_START;
static unsigned char level = 1;
static unsigned char have_treasure;      /* gates monster scoring, see DESIGN.md */
static unsigned char room_cleared;
static unsigned char dead;

static unsigned int  rng;
static unsigned char tick_count;

/* ---- RNG ---------------------------------------------------------------- */
unsigned int rnd16(void)
{
    /* xorshift; any nonzero seed cycles the full period. */
    rng ^= rng << 7;
    rng ^= rng >> 9;
    rng ^= rng << 8;
    return rng;
}

unsigned char rndn(unsigned char n)
{
    return n ? (unsigned char)(rnd16() % n) : 0;
}

/* ---- drawing ------------------------------------------------------------
 * Every draw goes through the VIC register port: point at a cell, write a glyph.
 * There is no frame buffer in the 64K map to poke, so the game keeps its own
 * idea of the world (grid[] plus the entity arrays) and repaints only the cells
 * that changed each tick. */
static void put_at(unsigned char rx, unsigned char ry, unsigned char ch,
                   unsigned char attr)
{
    vaddr((unsigned int)(ROOM_Y + ry) * SCR_W + ROOM_X + rx);
    vattr(attr);
    vputc(ch);
}

static void put_str(unsigned char col, unsigned char row, const char *s,
                    unsigned char attr)
{
    vaddr((unsigned int)row * SCR_W + col);
    vattr(attr);
    while (*s) vputc((unsigned char)*s++);
}

/* Repaint a room cell from the grid -- used to erase an entity that moved off it. */
static void restore(unsigned char rx, unsigned char ry)
{
    switch (grid[ry][rx]) {
        case T_WALL:   put_at(rx, ry, G_WALL,    A_WALL);   break;
        case T_TREAS:  put_at(rx, ry, G_TREAS_A, A_TREAS);  break;
        case T_CORPSE: put_at(rx, ry, G_CORPSE,  A_CORPSE); break;
        default:       put_at(rx, ry, G_FLOOR,   A_TEXT);   break;
    }
}

static void draw_room(void)
{
    unsigned char rx, ry;
    for (ry = 0; ry < ROOM_H; ry++)
        for (rx = 0; rx < ROOM_W; rx++)
            restore(rx, ry);
}

/* ---- HUD --------------------------------------------------------------- */
static void put_num(unsigned char col, unsigned char row, unsigned int v,
                    unsigned char width, unsigned char attr)
{
    char buf[6];
    signed char i = 0;
    vaddr((unsigned int)row * SCR_W + col);
    vattr(attr);
    if (!v) buf[i++] = '0';
    while (v) { buf[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (width-- > (unsigned char)i) vputc(' ');
    while (--i >= 0) vputc((unsigned char)buf[i]);
}

static void draw_hud(void)
{
    put_str(4,  HUD_ROW, "SCORE", A_HUD);
    put_num(10, HUD_ROW, score, 6, A_HUD);
    put_str(24, HUD_ROW, "LIVES", A_HUD);
    put_num(30, HUD_ROW, lives, 1, A_HUD);
    put_str(40, HUD_ROW, "LEVEL", A_HUD);
    put_num(46, HUD_ROW, level, 1, A_HUD);
    put_str(56, HUD_ROW, have_treasure ? "APPLES TAKEN" : "            ", A_TREAS);
}

/* The status line carries state, not advice. The game does not explain its own
 * rules -- that is what docs/VENTURE.md is for, in the same way an arcade cabinet
 * had an instruction card and the machine itself just played. */
static void msg(const char *s)
{
    put_str(4, MSG_ROW, "                                                       ",
            A_TEXT);
    put_str(4, MSG_ROW, s, A_TEXT);
}

/* ---- room setup -------------------------------------------------------- */
static void load_room(void)
{
    unsigned char rx, ry, c;

    mon_count = 0;
    for (rx = 0; rx < MAX_MON; rx++) m_live[rx] = 0;

    for (ry = 0; ry < ROOM_H; ry++) {
        for (rx = 0; rx < ROOM_W; rx++) {
            c = (unsigned char)room_1[ry][rx];
            switch (c) {
                case '#': grid[ry][rx] = T_WALL;  break;
                case '*': grid[ry][rx] = T_TREAS; break;
                case 'm':
                    grid[ry][rx] = T_FLOOR;
                    if (mon_count < MAX_MON) {
                        m_live[mon_count] = 1;
                        m_x[mon_count] = rx;
                        m_y[mon_count] = ry;
                        mon_count++;
                    }
                    break;
                default:  grid[ry][rx] = T_FLOOR; break;
            }
        }
    }

    /* Winky starts bottom-right, away from the treasure, so the room has to be
     * crossed rather than opened with a step. */
    wx = ROOM_W - 3;
    wy = ROOM_H - 3;
    face_dx = 0;
    face_dy = -1;
    a_live = 0;
    have_treasure = 0;
    room_cleared = 0;
    dead = 0;
}

/* ---- collision --------------------------------------------------------- */
static unsigned char blocked(unsigned char rx, unsigned char ry)
{
    return grid[ry][rx] == T_WALL;
}

/* A cell that kills Winky on contact: a live monster or any body left behind.
 * Venture's signature cruelty is that clearing a room fills it with hazards. */
static unsigned char lethal(unsigned char rx, unsigned char ry)
{
    unsigned char i;
    if (grid[ry][rx] == T_CORPSE) return 1;
    for (i = 0; i < MAX_MON; i++)
        if (m_live[i] && m_x[i] == rx && m_y[i] == ry) return 1;
    return 0;
}

/* ---- Winky ------------------------------------------------------------- */
static void winky_move(unsigned char ks)
{
    signed char dx = 0, dy = 0;
    unsigned char nx, ny;

    if (ks & KS_LEFT)  dx = -1;
    if (ks & KS_RIGHT) dx =  1;
    if (ks & KS_UP)    dy = -1;
    if (ks & KS_DOWN)  dy =  1;

    if (!dx && !dy) return;

    /* Facing persists after the key is released, so you can back out of a room
     * while still aiming into it. */
    face_dx = dx;
    face_dy = dy;

    /* Axes are resolved separately so a diagonal into a wall still slides along
     * it instead of stopping dead -- with one arrow and lethal corpses, getting
     * stuck on a corner would be a cheap death. */
    nx = (unsigned char)(wx + dx);
    ny = wy;
    if (dx && !blocked(nx, ny)) wx = nx;

    nx = wx;
    ny = (unsigned char)(wy + dy);
    if (dy && !blocked(nx, ny)) wy = ny;
}

static void winky_touch(void)
{
    if (grid[wy][wx] == T_TREAS) {
        grid[wy][wx] = T_FLOOR;
        have_treasure = 1;
        score += (unsigned int)SCORE_TREASURE * level;
        draw_hud();
    }
    if (lethal(wx, wy)) dead = 1;
}

/* ---- arrow ------------------------------------------------------------- */
static void fire(void)
{
    if (a_live) return;               /* one shot in flight, as the original */
    a_x = wx;
    a_y = wy;
    a_dx = face_dx;
    a_dy = face_dy;
    a_live = 1;
}

static void arrow_advance(void)
{
    unsigned char step, i, nx, ny;

    if (!a_live) return;

    for (step = 0; step < ARROW_STEP; step++) {
        nx = (unsigned char)(a_x + a_dx);
        ny = (unsigned char)(a_y + a_dy);

        /* The arrow dies here, so nothing later repaints the cell it is currently
         * drawn in -- step()'s restore pass is gated on a_live. Repaint it now or
         * the glyph is left behind as litter. */
        if (blocked(nx, ny)) { a_live = 0; restore(a_x, a_y); return; }

        a_x = nx;
        a_y = ny;

        for (i = 0; i < MAX_MON; i++) {
            if (m_live[i] && m_x[i] == a_x && m_y[i] == a_y) {
                m_live[i] = 0;
                /* The body stays, and stays lethal. */
                grid[a_y][a_x] = T_CORPSE;
                a_live = 0;
                /* Draw it now. Nothing else will: the monster is off the live list
                 * and so is the arrow, and step() only restores cells belonging to
                 * things still alive. Without this the body sits in the grid --
                 * lethal, and killing you -- while the screen still shows an arrow
                 * or a serpent in that cell. */
                restore(a_x, a_y);
                /* The original's rule: monsters pay nothing until the treasure
                 * is yours. Clearing the room first is the safe play and scores
                 * you nothing for it. */
                if (have_treasure) {
                    score += (unsigned int)SCORE_MONSTER * level;
                    draw_hud();
                }
                return;
            }
        }
    }
}

/* ---- monsters ---------------------------------------------------------- */
/* A plain greedy step toward Winky, one axis at a time, preferring whichever
 * axis is further away. Slower than Winky (MON_EVERY) and easily out-manoeuvred
 * in the open, which is the point: the danger is being cornered, not outrun. */
static void monsters_advance(void)
{
    unsigned char i, nx, ny;
    signed char dx, dy;

    for (i = 0; i < MAX_MON; i++) {
        if (!m_live[i]) continue;

        dx = (wx > m_x[i]) ? 1 : (wx < m_x[i]) ? -1 : 0;
        dy = (wy > m_y[i]) ? 1 : (wy < m_y[i]) ? -1 : 0;

        nx = m_x[i];
        ny = m_y[i];

        if (dx && (!dy || (unsigned char)(wx > m_x[i] ? wx - m_x[i] : m_x[i] - wx)
                        >= (unsigned char)(wy > m_y[i] ? wy - m_y[i] : m_y[i] - wy))) {
            if (!blocked((unsigned char)(m_x[i] + dx), ny)) nx = (unsigned char)(m_x[i] + dx);
            else if (dy && !blocked(nx, (unsigned char)(m_y[i] + dy)))
                ny = (unsigned char)(m_y[i] + dy);
        } else if (dy) {
            if (!blocked(nx, (unsigned char)(m_y[i] + dy))) ny = (unsigned char)(m_y[i] + dy);
            else if (dx && !blocked((unsigned char)(m_x[i] + dx), ny))
                nx = (unsigned char)(m_x[i] + dx);
        }

        /* Monsters do not walk through bodies either -- the room closes in on
         * them as much as on you. */
        if (grid[ny][nx] != T_CORPSE) {
            m_x[i] = nx;
            m_y[i] = ny;
        }

        if (m_x[i] == wx && m_y[i] == wy) dead = 1;
    }
}

/* ---- one simulation step ---------------------------------------------- */
static void step(unsigned char ks)
{
    unsigned char i;

    /* Erase everything that can move, from the grid underneath it. */
    restore(wx, wy);
    if (a_live) restore(a_x, a_y);
    for (i = 0; i < MAX_MON; i++)
        if (m_live[i]) restore(m_x[i], m_y[i]);

    tick_count++;

    winky_move(ks);
    winky_touch();
    if (ks & KS_FIRE) fire();

    arrow_advance();
    if (!(tick_count % MON_EVERY)) monsters_advance();

    winky_touch();               /* a monster may have stepped onto Winky */

    /* Redraw in an order that keeps Winky visible: he is the thing the player
     * is tracking, so he wins any overlap. */
    for (i = 0; i < MAX_MON; i++)
        if (m_live[i]) put_at(m_x[i], m_y[i], G_SERPENT, A_MON);
    if (a_live)
        put_at(a_x, a_y,
               a_dy < 0 ? G_ARROW_U : a_dy > 0 ? G_ARROW_D :
               a_dx < 0 ? G_ARROW_L : G_ARROW_R, A_ARROW);

    anim ^= 1;
    put_at(wx, wy, anim ? G_WINKY : G_WINKY_2, A_WINKY);

    /* Leaving by the top edge with the treasure clears the room. Until the
     * dungeon map exists (step 6) that is the exit. */
    if (have_treasure && wy <= 1) room_cleared = 1;
}

/* ---- keyboard FIFO ------------------------------------------------------
 * Movement and fire come off the control port, so the only things read from the
 * keystroke buffer are commands. But the host sends an arrow key BOTH ways: it
 * sets the control-port bit and pushes the ANSI sequence ESC [ A. So those bytes
 * arrive here whether we want them or not, and a bare ESC can never be treated as
 * a command -- every arrow starts with one, which is ambiguous by construction.
 * (Treating ESC as quit meant any arrow key exited the game instantly.)
 *
 * Venture does not need to decode arrows at all; it only needs their bytes not to
 * be mistaken for commands. So swallow the whole three-byte sequence. The state
 * persists across calls because in a real-time loop the bytes genuinely can arrive
 * on separate passes. */
static unsigned char esc_state;      /* 0 idle, 1 saw ESC, 2 saw ESC '[' */

static unsigned char swallow_esc(int k)
{
    switch (esc_state) {
        case 0:
            if (k == 0x1B) { esc_state = 1; return 1; }
            return 0;
        case 1:
            esc_state = (k == '[') ? 2 : 0;
            return 1;
        default:
            esc_state = 0;           /* the final letter (A/B/C/D) */
            return 1;
    }
}

/* ---- shell ------------------------------------------------------------- */
static void banner(const char *line)
{
    vfill(' ');
    vcmd(VCMD_CLEAR);
    put_str(33, 11, line, A_HUD);
    while (INCH_NB() < 0) { }
}

int main(void)
{
    unsigned int last, now;
    unsigned char ks, catchup;
    int k;

    rng = rng_seed();
    if (!rng) rng = 0xACE1;

    vhidecur();

    banner("V E N T U R E");

    for (;;) {
        vfill(' ');
        vcmd(VCMD_CLEAR);
        load_room();
        draw_room();
        draw_hud();

        last = jiffies();
        for (;;) {
            /* Fixed-timestep accumulator, all integer, unsigned subtraction so
             * the 60 Hz counter wrapping every ~18 minutes does not matter. */
            now = jiffies();
            if ((unsigned int)(now - last) >= TICK_RATE) {
                ks = keystate();
                for (catchup = 0;
                     (unsigned int)(now - last) >= TICK_RATE && catchup < MAX_CATCHUP;
                     catchup++) {
                    step(ks);
                    last += TICK_RATE;
                    if (dead || room_cleared) break;
                }
                /* If we are still behind after the catch-up budget, drop the
                 * backlog rather than sprinting to make it up. */
                if ((unsigned int)(now - last) >= TICK_RATE) last = now;
            }

            if (dead || room_cleared) break;

            /* The FIFO carries the commands that are not movement. Drained a few
             * per pass so a burst cannot back up and start dropping bytes. */
            for (catchup = 0; catchup < 4; catchup++) {
                k = INCH_NB();
                if (k < 0) break;
                if (swallow_esc(k)) continue;   /* an arrow's ESC [ X, not a command */
                if (k == 'q' || k == 'Q') { QUITDOS(); return 0; }
                if (k == 'p' || k == 'P') {
                    msg("PAUSED");
                    while (INCH_NB() < 0) { }
                    msg("");
                    last = jiffies();
                }
            }
        }

        if (room_cleared) {
            banner("ROOM CLEARED");
            level++;
        } else {
            if (lives) lives--;
            if (!lives) {
                banner("G A M E   O V E R");
                QUITDOS();
                return 0;
            }
            banner("CAUGHT");
        }
    }
}
