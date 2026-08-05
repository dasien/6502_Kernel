/*
 * VENTURE for MFC -- a port of Exidy's Venture (1981).
 *
 * The whole game: a dungeon map you walk between rooms, six themed rooms each
 * holding a treasure and the things guarding it, Hallmonsters that cannot be
 * killed and come after you if you dawdle, three levels that loop faster every
 * time round, and no ending at all -- you play until the lives run out.
 *
 * The game explains nothing while it runs. docs/VENTURE.md is the instruction
 * card; an arcade cabinet carried one bolted to the side and the machine itself
 * just played.
 */

#include "venture.h"

/* ---- layouts -------------------------------------------------------------
 * Boards are held as a string per row so they can be read and edited as
 * pictures, and turned into a tile grid at load time. The screen sits behind a
 * register port with no random-access read, so the game keeps its own idea of
 * what each cell *is* (floor, wall, corpse, door) separately from what it looks
 * like.
 *
 * Map:  '#' wall  '.' hall  '1'-'4' the door into room slot 0-3  'h' a
 *       Hallmonster's starting post.
 * Room: '#' wall  '.' floor  '*' the treasure  'm' a monster post
 *       'd' the doorway in the top wall, which is both entrance and exit.
 *
 * Every layout is checked at build time by tests/test_venture.cpp: exact
 * dimensions, a sealed border, and a flood fill proving the treasure and every
 * monster post are reachable from the door. A one-character typo that walls off
 * a treasure is otherwise an unwinnable room nobody notices until they play it.
 */

/* One map, reused by every level. What changes with the level is what is behind
 * the doors -- and the map is the part you have already learned by the time that
 * would matter. */
static const char *const map_layout[MAP_H] = {
    "########################################################",
    "#......................................................#",
    "#..######...................h...................######.#",
    "#..#.1....................................h.....2....#.#",
    "#..######.......................................######.#",
    "#......................................................#",
    "#..........########################################....#",
    "#..............................................#.......#",
    "#..........########################################....#",
    "#......................................................#",
    "#..######...............h.......................######.#",
    "#..#.3..........................................4....#.#",
    "#..######.......................................######.#",
    "#......................................................#",
    "########################################################",
};

/* Six themed rooms; a level deals four of them. One table rather than six named
 * arrays and a pointer list -- cc65 will not initialise a pointer table from
 * other arrays' names, and the table is what the code wants anyway. */
static const char *const room_art[THEMES][ROOM_H] = {
    {   /* 0 SERPENT */
        "######################d#####################",
        "#..........................................#",
        "#..####............................####....#",
        "#..#..........m...................#...#....#",
        "#..#....####.................####.#...#....#",
        "#.......#..#.................#..#..........#",
        "#.......#..#.......m.........#..#..........#",
        "#.......####.................####..........#",
        "#..........................................#",
        "#####..........####...####.............#####",
        "#..................#...#.......m...........#",
        "#..####............#...#...................#",
        "#..#..*............#####...................#",
        "#..#.......................................#",
        "############################################",
    },
    {   /* 1 CYCLOPS */
        "######################d#####################",
        "#..........................................#",
        "#....######################.........m......#",
        "#....#....................#................#",
        "#....#..*.................#......####......#",
        "#....#....................#......#..#......#",
        "#....######.......#########......#..#......#",
        "#.................#..............####......#",
        "#.......m.........#........................#",
        "#..................######..................#",
        "#.........######........#........m.........#",
        "#.........#....#........#..................#",
        "#.........#....#........#..................#",
        "#.........######........#..................#",
        "############################################",
    },
    {   /* 2 SPIDER */
        "######################d#####################",
        "#..........................................#",
        "#..#..#..#..#..#..#..#..#..#..#..#..#..#...#",
        "#..........................m...............#",
        "#..#..#..#..#..#..#..#..#..#..#..#..#..#...#",
        "#..........................................#",
        "#..#..#..#..####.####..#..#..#..#..#..#....#",
        "#..............#*#.........................#",
        "#..#..#..#..#..#.#.....#..#..#..#..#..#....#",
        "#........m.....###.........................#",
        "#..#..#..#..#..#..#..#..#..#..#..#..#..#...#",
        "#...........................m..............#",
        "#..#..#..#..#..#..#..#..#..#..#..#..#..#...#",
        "#..........................................#",
        "############################################",
    },
    {   /* 3 GOAT */
        "######################d#####################",
        "#..........................................#",
        "#...m..................................m...#",
        "#..........................................#",
        "#......######################..............#",
        "#......#....................#..............#",
        "#......#.........*..........#..............#",
        "#......#....................#..............#",
        "#......#####..........#######..............#",
        "#..........................................#",
        "#..............#########...................#",
        "#..............#.......#.......m...........#",
        "#..............#.......#...................#",
        "#..............#########...................#",
        "############################################",
    },
    {   /* 4 SKELETON */
        "######################d#####################",
        "#..........................................#",
        "#..######..######..######..######..######..#",
        "#.......#........#.......#.......#.........#",
        "#...m...#........#...*...#.......#....m....#",
        "#.......#........#.......#.......#.........#",
        "#..######..######..#...##..######..######..#",
        "#..........................................#",
        "#..######..######..######..######..######..#",
        "#.......#........#.......#.......#.........#",
        "#.......#....m...#.......#.......#.........#",
        "#.......#........#.......#.......#.........#",
        "#..######..######..######..######..######..#",
        "#..........................................#",
        "############################################",
    },
    {   /* 5 WRAITH */
        "######################d#####################",
        "#..........................................#",
        "#..............m...........................#",
        "#.....############...############..........#",
        "#.....#..........#...#..........#..........#",
        "#.....#...*......#...#..........#....m.....#",
        "#.....#..........#...#..........#..........#",
        "#.....#####..#####...#####..#####..........#",
        "#..........................................#",
        "#..........#####################...........#",
        "#..........#...................#...........#",
        "#....m.....#...................#...........#",
        "#..........#####################...........#",
        "#..........................................#",
        "############################################",
    },
};

/* Glyphs chosen by rendering the character ROM and reading the shapes; the names
 * in docs/VENTURE.md follow the glyphs, not the other way round. */
static const unsigned char theme_monster[THEMES]  = {
    0x15, 0xE9, 0x0F, 0xEA, 0x9D, 0xE8
};
static const unsigned char theme_treasure[THEMES] = {
    0x05, 0x04, 0x09, 0xFE, 0x0A, 0x03
};

/* ---- the board on screen -----------------------------------------------
 * Map and room share one grid and one set of drawing and pursuit routines. They
 * differ only in dimensions, what a door means, and whether you are allowed to
 * shoot -- which is why the hall is nearly free. */
static unsigned char grid[MAP_H][MAP_W];   /* the map is the larger of the two */
static unsigned char gw, gh;        /* the current board's dimensions */
static unsigned char gx0, gy0;      /* and where it is drawn on screen */

static unsigned char mode;          /* MODE_MAP / MODE_ROOM */
static unsigned char theme;         /* which of the six rooms we are inside */
static unsigned char room_of_slot[ROOMS_PER_LEVEL];
static unsigned char slot_done[ROOMS_PER_LEVEL];
static unsigned char slot_entered = 0xFF;   /* the door we last went through */

/* ---- entities ---------------------------------------------------------- */
static unsigned char wx, wy;             /* Winky, in board coordinates */
static signed char   face_dx, face_dy;   /* last direction held; arrows use it */
static unsigned char anim;               /* toggles the two Winky frames */

static unsigned char m_live[MAX_MON], m_x[MAX_MON], m_y[MAX_MON];
static unsigned char h_live[MAX_HALL], h_x[MAX_HALL], h_y[MAX_HALL];

static unsigned char a_live;             /* one arrow in flight, as the original */
static unsigned char a_x, a_y;
static signed char   a_dx, a_dy;

/* ---- game state -------------------------------------------------------- */
static unsigned int  score;
static unsigned char lives = LIVES_START;
static unsigned char level = 1;
static unsigned char tickrate = TICK_RATE;   /* the whole difficulty ramp */
static unsigned char have_treasure;          /* gates monster scoring */
static unsigned char left_room, dead, level_done;
static unsigned int  dawdle;                 /* ticks spent in the current room */

static unsigned int  rng;
static unsigned char tick_count;
static unsigned char snd_left;               /* ticks a sound cue still has to run */

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

/* ---- sound --------------------------------------------------------------
 * Cues, not a score: start a tone, let it run a few ticks, gate it off again.
 * The kernel honours SOUND_ENABLE, so a muted machine simply stays quiet and the
 * game does not have to know. */
static void cue(unsigned int freq)
{
    sound_tone(freq);
    snd_left = SND_TICKS;
}

static void cue_tick(void)
{
    if (snd_left && !--snd_left) sound_off();
}

/* ---- drawing ------------------------------------------------------------
 * Every draw goes through the VIC register port: point at a cell, write a glyph.
 * There is no frame buffer in the 64K map to poke, so the game repaints only the
 * cells that changed each tick. */
static void put_at(unsigned char rx, unsigned char ry, unsigned char ch,
                   unsigned char attr)
{
    vaddr((unsigned int)(gy0 + ry) * SCR_W + gx0 + rx);
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

/* Repaint a cell from the grid -- used to erase an entity that moved off it.
 *
 * A door carries its room's state: bright while there is still treasure behind
 * it, dim once there is not. That is the only thing the map ever tells you about
 * a room, and it only tells you after you have already been inside. Rooms stay
 * blind from the hall, which is where the dread lives. */
static void restore(unsigned char rx, unsigned char ry)
{
    const unsigned char t = grid[ry][rx];
    if (t >= T_DOOR0) {
        if (slot_done[t - T_DOOR0]) put_at(rx, ry, G_CLEARED, A_CLEARED);
        else                        put_at(rx, ry, G_DOOR,    A_DOOR);
        return;
    }
    switch (t) {
        case T_WALL:   put_at(rx, ry, G_WALL, A_WALL); break;
        case T_TREAS:  put_at(rx, ry, theme_treasure[theme], A_TREAS); break;
        case T_CORPSE: put_at(rx, ry, G_CORPSE, A_CORPSE); break;
        default:       put_at(rx, ry, G_FLOOR, A_TEXT); break;
    }
}

static void draw_board(void)
{
    unsigned char rx, ry;
    for (ry = 0; ry < gh; ry++)
        for (rx = 0; rx < gw; rx++)
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
}

/* The status line carries state, never advice. The game does not explain its own
 * rules -- that is what docs/VENTURE.md is for, in the same way an arcade cabinet
 * had an instruction card and the machine itself just played. */
static void msg(const char *s)
{
    put_str(4, MSG_ROW, "                                                       ",
            A_TEXT);
    put_str(4, MSG_ROW, s, A_TEXT);
}

/* ---- loading a board --------------------------------------------------- */
static unsigned char blocked(unsigned char rx, unsigned char ry)
{
    return grid[ry][rx] == T_WALL;
}

static void load_board(const char *const *art, unsigned char w, unsigned char h,
                       unsigned char ox, unsigned char oy)
{
    unsigned char rx, ry, c, nm = 0, nh = 0;

    gw = w; gh = h; gx0 = ox; gy0 = oy;
    for (rx = 0; rx < MAX_MON; rx++)  m_live[rx] = 0;
    for (rx = 0; rx < MAX_HALL; rx++) h_live[rx] = 0;
    a_live = 0;

    for (ry = 0; ry < h; ry++) {
        for (rx = 0; rx < w; rx++) {
            c = (unsigned char)art[ry][rx];
            switch (c) {
                case '#': grid[ry][rx] = T_WALL;  break;
                case '*': grid[ry][rx] = T_TREAS; break;
                case 'm':
                    grid[ry][rx] = T_FLOOR;
                    if (nm < MAX_MON) {
                        m_live[nm] = 1; m_x[nm] = rx; m_y[nm] = ry; nm++;
                    }
                    break;
                case 'h':
                    grid[ry][rx] = T_FLOOR;
                    if (nh < MAX_HALL) {
                        h_live[nh] = 1; h_x[nh] = rx; h_y[nh] = ry; nh++;
                    }
                    break;
                case '1': case '2': case '3': case '4':
                    grid[ry][rx] = (unsigned char)(T_DOOR0 + (c - '1'));
                    break;
                default:  grid[ry][rx] = T_FLOOR; break;   /* '.', and 'd' */
            }
        }
    }
}

/* Which slot's door is at this cell, or 0xFF for none. */
static unsigned char door_at(unsigned char rx, unsigned char ry)
{
    const unsigned char t = grid[ry][rx];
    return (t >= T_DOOR0) ? (unsigned char)(t - T_DOOR0) : 0xFF;
}

/* Put Winky back in the hall NEXT to the door he came out of, never on it.
 * Standing on the door would re-enter the room instantly, which after a death in
 * an uncleared room is an inescapable loop. */
static void place_at_door(void)
{
    unsigned char rx, ry;

    wx = MAP_START_X;
    wy = MAP_START_Y;
    if (slot_entered >= ROOMS_PER_LEVEL) return;

    for (ry = 0; ry < MAP_H; ry++) {
        for (rx = 0; rx < MAP_W; rx++) {
            if (door_at(rx, ry) != slot_entered) continue;
            if (rx + 1 < MAP_W && !blocked(rx + 1, ry)) { wx = rx + 1; wy = ry; }
            else if (rx && !blocked(rx - 1, ry))        { wx = rx - 1; wy = ry; }
            else if (ry + 1 < MAP_H && !blocked(rx, ry + 1)) { wx = rx; wy = ry + 1; }
            else if (ry && !blocked(rx, ry - 1))        { wx = rx; wy = ry - 1; }
            return;
        }
    }
}

static void enter_map(void)
{
    mode = MODE_MAP;
    load_board(map_layout, MAP_W, MAP_H, MAP_X, MAP_Y);
    place_at_door();
    face_dx = 0; face_dy = -1;
    dawdle = 0;

    vfill(' '); vcmd(VCMD_CLEAR);
    draw_board();
    draw_hud();
}

static void enter_room(unsigned char slot)
{
    unsigned char rx;

    mode = MODE_ROOM;
    slot_entered = slot;
    theme = room_of_slot[slot];
    load_board(room_art[theme], ROOM_W, ROOM_H, ROOM_X, ROOM_Y);

    /* Winky arrives just inside the doorway cut into the top wall. */
    wx = ROOM_W / 2;
    for (rx = 0; rx < ROOM_W; rx++)
        if (room_art[theme][0][rx] == 'd') { wx = rx; break; }
    wy = 1;
    face_dx = 0; face_dy = 1;

    have_treasure = 0;
    left_room = 0;
    dawdle = 0;

    vfill(' '); vcmd(VCMD_CLEAR);
    draw_board();
    draw_hud();
}

/* ---- collision --------------------------------------------------------- */
/* A cell that kills Winky on contact: a live monster, a Hallmonster, or any body
 * left behind. Venture's signature cruelty is that clearing a room fills it with
 * hazards. */
static unsigned char lethal(unsigned char rx, unsigned char ry)
{
    unsigned char i;
    if (grid[ry][rx] == T_CORPSE) return 1;
    for (i = 0; i < MAX_MON; i++)
        if (m_live[i] && m_x[i] == rx && m_y[i] == ry) return 1;
    for (i = 0; i < MAX_HALL; i++)
        if (h_live[i] && h_x[i] == rx && h_y[i] == ry) return 1;
    return 0;
}

/* ---- Winky ------------------------------------------------------------- */
static void winky_move(unsigned char ks)
{
    signed char dx = 0, dy = 0;
    unsigned char n;

    if (ks & KS_LEFT)  dx = -1;
    if (ks & KS_RIGHT) dx =  1;
    if (ks & KS_UP)    dy = -1;
    if (ks & KS_DOWN)  dy =  1;
    if (!dx && !dy) return;

    /* Facing persists after the key is released, so you can back out of a room
     * while still aiming into it. */
    face_dx = dx;
    face_dy = dy;

    /* Axes resolve separately so a diagonal into a wall slides along it instead
     * of stopping dead -- with one arrow and lethal bodies, sticking on a corner
     * would be a cheap death. Unsigned wrap makes 0-1 into 255, which the bound
     * check rejects, so the board edge needs no separate case. */
    n = (unsigned char)(wx + dx);
    if (dx && n < gw && !blocked(n, wy)) wx = n;
    n = (unsigned char)(wy + dy);
    if (dy && n < gh && !blocked(wx, n)) wy = n;
}

/* ---- arrow ------------------------------------------------------------- */
static void fire(void)
{
    if (a_live || mode != MODE_ROOM) return;   /* no shooting in the hall */
    a_x = wx; a_y = wy;
    a_dx = face_dx; a_dy = face_dy;
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
        if (nx >= gw || ny >= gh || blocked(nx, ny)) {
            a_live = 0; restore(a_x, a_y); return;
        }

        a_x = nx;
        a_y = ny;

        for (i = 0; i < MAX_MON; i++) {
            if (m_live[i] && m_x[i] == a_x && m_y[i] == a_y) {
                m_live[i] = 0;
                grid[a_y][a_x] = T_CORPSE;   /* the body stays, and stays lethal */
                a_live = 0;
                /* Draw it now. Nothing else will: the monster is off the live
                 * list and so is the arrow, and step() only restores cells
                 * belonging to things still alive. Without this the body sits in
                 * the grid -- lethal, and killing you -- while the screen still
                 * shows an arrow or a monster in that cell. */
                restore(a_x, a_y);
                /* The original's rule: monsters pay nothing until the treasure is
                 * yours. Clearing the room first is the safe play and scores you
                 * nothing for it. */
                if (have_treasure) {
                    score += (unsigned int)SCORE_MONSTER * level;
                    cue(SND_KILL);
                    draw_hud();
                }
                return;
            }
        }

        /* Hallmonsters cannot be shot. The arrow just stops on one. */
        for (i = 0; i < MAX_HALL; i++) {
            if (h_live[i] && h_x[i] == a_x && h_y[i] == a_y) {
                a_live = 0; restore(a_x, a_y); return;
            }
        }
    }
}

/* ---- pursuit ------------------------------------------------------------
 * A plain greedy step toward Winky, one axis at a time, preferring whichever
 * axis is further away. Slower than Winky and easily out-manoeuvred in the open,
 * which is the point: the danger is being cornered, not outrun. Room monsters and
 * Hallmonsters share it -- the only difference between them is that one can be
 * shot and one cannot. */
static void chase(unsigned char *px, unsigned char *py, unsigned char over_bodies)
{
    const unsigned char cx = *px, cy = *py;
    unsigned char nx = cx, ny = cy, adx, ady;
    const signed char dx = (wx > cx) ? 1 : (wx < cx) ? -1 : 0;
    const signed char dy = (wy > cy) ? 1 : (wy < cy) ? -1 : 0;

    adx = (unsigned char)(wx > cx ? wx - cx : cx - wx);
    ady = (unsigned char)(wy > cy ? wy - cy : cy - wy);

    if (dx && (!dy || adx >= ady)) {
        if (!blocked((unsigned char)(cx + dx), cy)) nx = (unsigned char)(cx + dx);
        else if (dy && !blocked(cx, (unsigned char)(cy + dy)))
            ny = (unsigned char)(cy + dy);
    } else if (dy) {
        if (!blocked(cx, (unsigned char)(cy + dy))) ny = (unsigned char)(cy + dy);
        else if (dx && !blocked((unsigned char)(cx + dx), cy))
            nx = (unsigned char)(cx + dx);
    }

    /* Greedy pursuit walks into dead ends: the hall's long wall bands leave a
     * chaser pressed against one with Winky straight through it and no second axis
     * to try. Rather than let it stand there forever, wander. On the map that reads
     * as a patrol, which is what a Hallmonster is meant to be doing anyway. */
    if (nx == cx && ny == cy) {
        const unsigned char dir = rndn(4);
        const signed char wdx = (dir == 0) ? 1 : (dir == 1) ? -1 : 0;
        const signed char wdy = (dir == 2) ? 1 : (dir == 3) ? -1 : 0;
        const unsigned char tx = (unsigned char)(cx + wdx);
        const unsigned char ty = (unsigned char)(cy + wdy);
        if (tx < gw && ty < gh && !blocked(tx, ty)) { nx = tx; ny = ty; }
    }

    /* Room monsters will not walk over a body either -- the room closes in on
     * them as much as on you. A Hallmonster does not care about anything. */
    if (over_bodies || grid[ny][nx] != T_CORPSE) { *px = nx; *py = ny; }
}

static void monsters_advance(void)
{
    unsigned char i;
    for (i = 0; i < MAX_MON; i++) {
        if (!m_live[i]) continue;
        chase(&m_x[i], &m_y[i], 0);
        if (m_x[i] == wx && m_y[i] == wy) dead = 1;
    }
}

static void hall_advance(void)
{
    unsigned char i;
    for (i = 0; i < MAX_HALL; i++) {
        if (!h_live[i]) continue;
        chase(&h_x[i], &h_y[i], 1);
        if (h_x[i] == wx && h_y[i] == wy) dead = 1;
    }
}

/* Dawdle in a room and one comes through the door after you. It cannot be shot,
 * blocked or outrun for long; there is nothing to do about it except leave. This
 * is the clock that stops Venture being a leisurely looting exercise. */
static void hall_intrude(void)
{
    unsigned char rx;

    if (h_live[0]) return;
    for (rx = 0; rx < ROOM_W; rx++) {
        if (room_art[theme][0][rx] != 'd') continue;
        h_live[0] = 1; h_x[0] = rx; h_y[0] = 0;
        cue(SND_HALL);
        return;
    }
}

/* ---- one simulation step ---------------------------------------------- */
static void step(unsigned char ks)
{
    unsigned char i, slot;

    /* Erase everything that can move, from the grid underneath it. */
    restore(wx, wy);
    if (a_live) restore(a_x, a_y);
    for (i = 0; i < MAX_MON; i++)  if (m_live[i]) restore(m_x[i], m_y[i]);
    for (i = 0; i < MAX_HALL; i++) if (h_live[i]) restore(h_x[i], h_y[i]);

    tick_count++;
    cue_tick();
    winky_move(ks);

    if (mode == MODE_MAP) {
        /* Walking onto a door is the commitment. enter_room() rebuilds the whole
         * board underneath us, so this step ends here. */
        slot = door_at(wx, wy);
        if (slot != 0xFF && !slot_done[slot]) { enter_room(slot); return; }
        if (lethal(wx, wy)) { dead = 1; return; }
        if (!(tick_count % HALL_EVERY)) hall_advance();
    } else {
        if (grid[wy][wx] == T_TREAS) {
            grid[wy][wx] = T_FLOOR;
            have_treasure = 1;
            score += (unsigned int)SCORE_TREASURE * level;
            cue(SND_TREASURE);
            draw_hud();
        }
        /* The doorway in the top wall is the way out, and only with the goods. */
        if (have_treasure && wy == 0) { left_room = 1; return; }
        if (lethal(wx, wy)) { dead = 1; return; }

        if (ks & KS_FIRE) fire();
        arrow_advance();
        if (!(tick_count % MON_EVERY)) monsters_advance();
        if (++dawdle > HALL_ROOM_TICKS) hall_intrude();
        if (!(tick_count % HALL_IN_EVERY)) hall_advance();
    }

    if (lethal(wx, wy)) dead = 1;   /* something may have stepped onto Winky */
    if (dead) return;

    /* Redraw in an order that keeps Winky visible: he is the thing the player is
     * tracking, so he wins any overlap. */
    for (i = 0; i < MAX_MON; i++)
        if (m_live[i]) put_at(m_x[i], m_y[i], theme_monster[theme], A_MON);
    for (i = 0; i < MAX_HALL; i++)
        if (h_live[i]) put_at(h_x[i], h_y[i], G_HALLMON, A_HALLMON);
    if (a_live)
        put_at(a_x, a_y,
               a_dy < 0 ? G_ARROW_U : a_dy > 0 ? G_ARROW_D :
               a_dx < 0 ? G_ARROW_L : G_ARROW_R, A_ARROW);

    anim ^= 1;
    put_at(wx, wy, anim ? G_WINKY : G_WINKY_2, A_WINKY);
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

/* Deal this level's four rooms from the six themes, rotating with the level so a
 * run of twelve room-visits is not four layouts seen three times each. */
static void start_level(void)
{
    unsigned char i;
    for (i = 0; i < ROOMS_PER_LEVEL; i++) {
        room_of_slot[i] =
            (unsigned char)(((level - 1) * ROOMS_PER_LEVEL + i) % THEMES);
        slot_done[i] = 0;
    }
    level_done = 0;
}

int main(void)
{
    unsigned int last, now;
    unsigned char ks, catchup, i;
    int k;

    rng = rng_seed();
    if (!rng) rng = 0xACE1;

    vhidecur();
    banner("V E N T U R E");

    start_level();
    enter_map();

    for (;;) {
        dead = 0;
        left_room = 0;
        last = jiffies();

        for (;;) {
            /* Fixed-timestep accumulator, all integer, unsigned subtraction so
             * the 60 Hz counter wrapping every ~18 minutes does not matter. */
            now = jiffies();
            if ((unsigned int)(now - last) >= tickrate) {
                ks = keystate();
                for (catchup = 0;
                     (unsigned int)(now - last) >= tickrate && catchup < MAX_CATCHUP;
                     catchup++) {
                    step(ks);
                    last += tickrate;
                    if (dead || left_room) break;
                }
                /* If we are still behind after the catch-up budget, drop the
                 * backlog rather than sprinting to make it up. */
                if ((unsigned int)(now - last) >= tickrate) last = now;
            }

            if (dead || left_room) break;

            /* The FIFO carries the commands that are not movement. Drained a few
             * per pass so a burst cannot back up and start dropping bytes. */
            for (catchup = 0; catchup < 4; catchup++) {
                k = INCH_NB();
                if (k < 0) break;
                if (swallow_esc(k)) continue;   /* an arrow's ESC [ X, not a command */
                if (k == 'q' || k == 'Q') { sound_off(); QUITDOS(); return 0; }
                if (k == 'p' || k == 'P') {
                    msg("PAUSED");
                    while (INCH_NB() < 0) { }
                    msg("");
                    last = jiffies();
                }
            }
        }

        sound_off();
        snd_left = 0;

        if (left_room) {
            slot_done[slot_entered] = 1;

            level_done = 1;
            for (i = 0; i < ROOMS_PER_LEVEL; i++)
                if (!slot_done[i]) level_done = 0;

            if (level_done) {
                banner("LEVEL CLEARED");
                level++;
                /* Three levels, then round again -- faster. tickrate is the only
                 * speed constant in the game, so lowering it speeds up Winky, the
                 * monsters and the Hallmonsters together and keeps their relative
                 * pacing exactly as tuned. There is no ending: you play until the
                 * lives run out. */
                if (level > LEVELS) {
                    level = 1;
                    if (tickrate > 1) tickrate--;
                }
                start_level();
                slot_entered = 0xFF;
            }
            enter_map();
        } else {
            cue(SND_DEATH);
            if (lives) lives--;
            if (!lives) {
                banner("G A M E   O V E R");
                sound_off();
                QUITDOS();
                return 0;
            }
            banner("CAUGHT");
            sound_off();
            snd_left = 0;
            /* A death costs the room, not the level: back out to the hall, and
             * the room is still there to try again. */
            enter_map();
        }
    }
}
