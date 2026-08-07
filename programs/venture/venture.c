/*
 * VENTURE for MFC -- a port of Exidy's Venture (1981).
 *
 * The whole game: a dungeon hall you cross between rooms, six themed rooms each
 * holding a treasure and the things guarding it, Hallmonsters that cannot be
 * killed and come after you if you dawdle, three levels that loop faster every
 * time round, and no ending at all -- you play until the lives run out.
 *
 * The layouts, the two-doors-per-room structure, the way a looted room seals
 * itself solid, the growing Hallmonster count and the per-level palette all come
 * from reading screenshots of the arcade game rather than from a description of
 * it. See DESIGN.md for what was taken and what could not be.
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
 * what each cell *is* (floor, wall, corpse, doorway) separately from what it
 * looks like.
 *
 * Hall: '#' wall  '.' floor  'A'-'D' room 0-3's outline  'a'-'d' inside it
 *       'h' a Hallmonster post. Entrances are NOT in the data -- see cut_notches().
 * Room: '#' wall  '.' floor  '*' the treasure  'm' a monster post
 *       '+' a doorway in the border -- two of them, and either one is a way out.
 *
 * Every layout is checked by tests/test_venture.cpp: exact dimensions, a sealed
 * border, both doorways connected to each other and to the treasure and every
 * monster, and -- for the hall -- that sealing any one room cannot strand the
 * entrances of another. A one-character typo makes a room unwinnable and nothing
 * about the source looks wrong.
 */

/* The hall is an open arena with the four rooms drawn in it as hollow outlines, which
 * is how the arcade draws its dungeon floor. One hall serves every level; what
 * changes is what is behind the doors, and by the time that would matter you have
 * learnt the hall.
 *
 * The entrances are cut in at runtime rather than being part of this picture, because
 * where they go depends on which room the slot is holding -- see cut_notches(). */

static const char *const map_layout[MAP_H] = {
    "##############################",
    "#h............h.............h#",
    "#.AAAAAAA............BBBBBBB.#",
    "#.AaaaaaA............BbbbbbB.#",
    "#.AAAAAAA............BBBBBBB.#",
    "#............................#",
    "#.CCCCCCC............DDDDDDD.#",
    "#.CcccccC............DdddddD.#",
    "#.CCCCCCC............DDDDDDD.#",
    "#h............h.............h#",
    "##############################",
};

/* Six themed rooms; a level deals four of them. One table rather than six named
 * arrays and a pointer list -- cc65 will not initialise a pointer table from
 * other arrays' names, and the table is what the code wants anyway. */
static const char *const room_art[THEMES][ROOM_H] = {
    {   /* 0 SERPENT */
        "###############+##############",
        "#.........#..................#",
        "#.........#.....m............#",
        "#....m....#..................#",
        "#.........#..............m...#",
        "#.........#..................#",
        "#.........#############......#",
        "#............................#",
        "#...*........................#",
        "#............................#",
        "########################+#####",
    },
    {   /* 1 CYCLOPS */
        "###############+##############",
        "#............................#",
        "#..m.........................#",
        "#.......##############.......#",
        "#.......#............#.......#",
        "+..........m..*..............#",
        "#.......#............#.......#",
        "#.......##############.......#",
        "#.........................m..#",
        "#............................#",
        "##############################",
    },
    {   /* 2 SPIDER */
        "##########+###################",
        "#............................#",
        "#..#...#...#...#...#...#.....#",
        "#....m.......................#",
        "#.............*..............#",
        "#..#...#...#...#...#...#.....#",
        "#.......................m....#",
        "#............................#",
        "#..#...#...#..m#...#...#.....#",
        "#............................#",
        "####################+#########",
    },
    {   /* 3 GOAT */
        "##############################",
        "#............................#",
        "+............................#",
        "#...#########....#########...#",
        "#...#########....#########...#",
        "#.m.#########.*..#########.m.#",
        "#...#########....#########...#",
        "#...#########....#########...#",
        "#............................+",
        "#.............m..............#",
        "##############################",
    },
    {   /* 4 SKELETON */
        "####+#########################",
        "#...m........................#",
        "#............................#",
        "#####################........#",
        "#............................#",
        "#.............m..........*...#",
        "#............................#",
        "#........#####################",
        "#............................#",
        "#....m.......................#",
        "#########################+####",
    },
    {   /* 5 WRAITH */
        "##############################",
        "#.............#..............#",
        "+.............#.....m........#",
        "#.............#.........m....#",
        "#............................#",
        "##########....*....###########",
        "#............................#",
        "#.............#..............#",
        "#.......m.....#..............+",
        "#.............#..............#",
        "##############################",
    },
};



/* Glyph tables, indexed by theme. Chosen by rendering the character ROM and reading
 * the shapes; the names in docs/VENTURE.md follow the glyphs, not the other way
 * round. The colours give the level-start roster the arcade's multicoloured look. */
static const unsigned char theme_monster[THEMES]  = {
    0x15, 0xE9, 0x0F, 0xEA, 0x9D, 0xE8
};
static const unsigned char theme_treasure[THEMES] = {
    0x05, 0x04, 0x09, 0xFE, 0x0A, 0x03
};
static const unsigned char theme_attr[THEMES] = {
    0x42, 0x47, 0x45, 0x43, 0x46, 0x41
};

/* The arcade recolours the whole dungeon each level -- magenta, then cyan, then
 * yellow, with the Hallmonsters on a colour of their own. Cheap here, because the
 * attribute plane is a separate write from the glyph. */
static const unsigned char lvl_wall[LEVELS] = { 0x05, 0x06, 0x03 };
static const unsigned char lvl_room[LEVELS] = { 0x45, 0x46, 0x43 };
static const unsigned char lvl_hall[LEVELS] = { 0x42, 0x45, 0x46 };

static unsigned char a_wall, a_room, a_hall;   /* this level's three */

/* ---- the board on screen -----------------------------------------------
 * Hall and room share one grid and one set of drawing and pursuit routines. They
 * differ only in dimensions, in what a door means, and in whether you are allowed
 * to shoot -- which is why the hall costs so little. */
static unsigned char grid[MAP_H][MAP_W];   /* the hall is the larger of the two */
static unsigned char gw, gh;               /* the current board's dimensions */
static unsigned char gx0, gy0;             /* and where it is drawn on screen */

/* Which side of a board a border cell sits on. This is the whole of the door
 * alignment scheme, and it runs both ways: you come INTO a room at the doorway on the
 * same side as the hall entrance you used, and you come back OUT of the hall entrance
 * on the same side as the doorway you left by. Enter a room from the east and leave it
 * by its north door, and you step back into the hall at the north of its block.
 *
 * Which is why the entrances cannot be drawn into map_layout: a slot holds a different
 * room each level, and the sides have to follow it. */
#define SIDE_N 0
#define SIDE_S 1
#define SIDE_W 2
#define SIDE_E 3

/* The four room blocks in the hall, matching map_layout above. */
#define BLK_W 7
#define BLK_H 3
static const unsigned char blk_x[ROOMS_PER_LEVEL] = { 2, 21, 2, 21 };
static const unsigned char blk_y[ROOMS_PER_LEVEL] = { 2,  2, 6,  6 };

static unsigned char sl_side[ROOMS_PER_LEVEL][2];   /* each slot's two door sides */
static unsigned char sl_x[ROOMS_PER_LEVEL][2];      /* and where they got cut */
static unsigned char sl_y[ROOMS_PER_LEVEL][2];

static unsigned char rx_side[2], rx_x[2], rx_y[2];  /* the current room's doorways */
static unsigned char exit_used;                     /* which one we walked out of */

static unsigned char mode;                 /* MODE_MAP / MODE_ROOM */
static unsigned char theme;                /* which of the six rooms we are in */
static unsigned char room_of_slot[ROOMS_PER_LEVEL];
static unsigned char slot_done[ROOMS_PER_LEVEL];
static unsigned char slot_entered = 0xFF;  /* the room we last went into */
static unsigned char ret_x, ret_y;         /* the hall cell we went in from */

/* ---- entities ---------------------------------------------------------- */
static unsigned char wx, wy;             /* Winky, in board coordinates */
static signed char   face_dx, face_dy;   /* last direction held; arrows use it */
static unsigned char anim;               /* toggles the two Winky frames */

static unsigned char f_live, f_x, f_y;   /* the facing pip, so it can be erased */

static unsigned char m_live[MAX_MON], m_x[MAX_MON], m_y[MAX_MON];
static unsigned char h_live[MAX_HALL], h_x[MAX_HALL], h_y[MAX_HALL];

static unsigned char a_live;             /* one arrow in flight, as the original */
static unsigned char a_x, a_y;
static signed char   a_dx, a_dy;

/* ---- game state -------------------------------------------------------- */
static unsigned int  score;
static unsigned int  level_base;         /* score when this level started */
static unsigned char lives = LIVES_START;
static unsigned char level = 1;
static unsigned char tickrate = TICK_RATE;   /* the whole difficulty ramp */
static unsigned char have_treasure;          /* gates monster scoring */
static unsigned char left_room, dead, level_done;
static unsigned char treas_got;              /* bit per theme, for the roster */
static unsigned int  dawdle;                 /* ticks spent in the current room */

static unsigned int  rng;
static unsigned char tick_count;
static unsigned char snd_left;           /* ticks a sound cue still has to run */

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
/* One playfield tile. The band is double-size rows, so logical row N lives on
 * physical row PLAY_ROW0 + 2N -- the row between is the bottom half of the glyphs
 * above it and is never addressed. Columns are one-to-one: a double row simply has
 * forty of them instead of eighty. */
static void put_at(unsigned char rx, unsigned char ry, unsigned char ch,
                   unsigned char attr)
{
    vaddr((unsigned int)(PLAY_ROW0 + (((gy0 + ry) << 1))) * SCR_W + gx0 + rx);
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
 * A room in the hall carries its state, and it is the only thing the hall ever tells
 * you: a hollow outline while there is still treasure in there, filled in solid with
 * its entrances sealed once there is not. That is the arcade's own signal, and it
 * only appears after you have already been inside. */
static void restore(unsigned char rx, unsigned char ry)
{
    const unsigned char t = grid[ry][rx];

    if (t >= T_DOOR0) {                  /* an entrance notched into the outline */
        if (slot_done[t - T_DOOR0]) put_at(rx, ry, G_SEALED, a_room);
        else                        put_at(rx, ry, G_DOOR,   A_DOOR);
        return;
    }
    if (t >= T_VOID0) {                  /* inside the outline: black, or filled */
        if (slot_done[t - T_VOID0]) put_at(rx, ry, G_SEALED, a_room);
        else                        put_at(rx, ry, G_FLOOR,  A_TEXT);
        return;
    }
    if (t >= T_BLK0) {                   /* the outline itself, either way */
        put_at(rx, ry, G_WALL, a_room);
        return;
    }
    switch (t) {
        case T_WALL:   put_at(rx, ry, G_WALL, a_wall); break;
        case T_TREAS:  put_at(rx, ry, theme_treasure[theme], theme_attr[theme]); break;
        case T_CORPSE: put_at(rx, ry, G_CORPSE, A_CORPSE); break;
        case T_EXIT:   put_at(rx, ry, G_DOORWAY, A_DOORWAY); break;
        default:       put_at(rx, ry, G_FLOOR, A_TEXT); break;
    }
}

/* Flag the playfield's rows double. A clear resets every row to normal -- that is
 * the VIC keeping a crashed program from stranding the shell at 16x32 -- so this has
 * to run after each one, before anything is drawn into the band. */
static void set_play_rows(void)
{
    unsigned char i;
    for (i = 0; i < ROOM_H; i++) {
        vfill((unsigned char)(VROW_DOUBLE | (PLAY_ROW0 + (i << 1))));
        vcmd(VCMD_ROWSIZE);
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
    const unsigned char t = grid[ry][rx];
    if (t == T_WALL) return 1;
    if (t >= T_DOOR0) return slot_done[t - T_DOOR0];   /* sealed once looted */
    if (t >= T_BLK0)  return 1;   /* outline and interior alike: a room in the hall
                                   * is solid, and the only way in is an entrance */
    return 0;
}

/* Where a pursuer may not go. Beyond the walls, that means a room's entrance: it
 * is a single cell let into a solid block, so a chaser that steps in has nothing
 * to step to and spends the rest of the level rattling in the doorway. They are
 * patrolling the hall, not queueing to get into the rooms. */
static unsigned char pursuit_blocked(unsigned char rx, unsigned char ry)
{
    return blocked(rx, ry) || grid[ry][rx] >= T_DOOR0;
}

static void load_board(const char *const *art, unsigned char w, unsigned char h,
                       unsigned char ox, unsigned char oy)
{
    unsigned char rx, ry, c, nm = 0, nh = 0;

    gw = w; gh = h; gx0 = ox; gy0 = oy;
    for (rx = 0; rx < MAX_MON; rx++)  m_live[rx] = 0;
    for (rx = 0; rx < MAX_HALL; rx++) h_live[rx] = 0;
    a_live = 0;
    f_live = 0;

    for (ry = 0; ry < h; ry++) {
        for (rx = 0; rx < w; rx++) {
            c = (unsigned char)art[ry][rx];
            switch (c) {
                case '#': grid[ry][rx] = T_WALL;  break;
                case '*': grid[ry][rx] = T_TREAS; break;
                case '+': grid[ry][rx] = T_EXIT;  break;
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
                case 'A': case 'B': case 'C': case 'D':
                    grid[ry][rx] = (unsigned char)(T_BLK0 + (c - 'A'));
                    break;
                case 'a': case 'b': case 'c': case 'd':
                    grid[ry][rx] = (unsigned char)(T_VOID0 + (c - 'a'));
                    break;
                default:  grid[ry][rx] = T_FLOOR; break;
            }
        }
    }
}

/* Read a room's two doorways off its border, in a fixed N-S-W-E order so the room's
 * list and its slot's list line up index for index. The scan skips the corners, which
 * is why a doorway may never be in one -- its side would be ambiguous, and
 * tests/test_venture.cpp enforces it. */
static unsigned char exits_of(unsigned char th, unsigned char *side,
                              unsigned char *ex, unsigned char *ey)
{
    unsigned char i, n = 0;
    for (i = 1; i < ROOM_W - 1; i++)
        if (room_art[th][0][i] == '+') {
            side[n] = SIDE_N; ex[n] = i; ey[n] = 0; n++;
        }
    for (i = 1; i < ROOM_W - 1; i++)
        if (room_art[th][ROOM_H - 1][i] == '+') {
            side[n] = SIDE_S; ex[n] = i; ey[n] = ROOM_H - 1; n++;
        }
    for (i = 1; i < ROOM_H - 1; i++)
        if (room_art[th][i][0] == '+') {
            side[n] = SIDE_W; ex[n] = 0; ey[n] = i; n++;
        }
    for (i = 1; i < ROOM_H - 1; i++)
        if (room_art[th][i][ROOM_W - 1] == '+') {
            side[n] = SIDE_E; ex[n] = ROOM_W - 1; ey[n] = i; n++;
        }
    return n;
}

/* Cut each slot's two entrances into the middle of the block edges that match its
 * room's doorway sides. The middle is close enough -- the SIDE is what a player reads,
 * and an 11-cell edge has nowhere meaningfully different to put it. */
static void cut_notches(void)
{
    unsigned char s, k, x, y;
    for (s = 0; s < ROOMS_PER_LEVEL; s++)
        for (k = 0; k < 2; k++) {
            x = blk_x[s];
            y = blk_y[s];
            switch (sl_side[s][k]) {
                case SIDE_N: x += BLK_W / 2;                     break;
                case SIDE_S: x += BLK_W / 2; y += BLK_H - 1;     break;
                case SIDE_W:                 y += BLK_H / 2;     break;
                default:     x += BLK_W - 1; y += BLK_H / 2;     break;
            }
            grid[y][x] = (unsigned char)(T_DOOR0 + s);
            sl_x[s][k] = x;
            sl_y[s][k] = y;
        }
}

/* The cell just inside a border doorway. */
static void step_inward(unsigned char dx, unsigned char dy)
{
    wx = dx; wy = dy;
    face_dx = 0; face_dy = 0;
    if (dy == 0)          { wy = 1;          face_dy =  1; }
    else if (dy == gh - 1) { wy = gh - 2;    face_dy = -1; }
    else if (dx == 0)      { wx = 1;         face_dx =  1; }
    else                   { wx = gw - 2;    face_dx = -1; }
}

/* Wake HALL_BASE Hallmonsters plus one per room already looted, and put the rest
 * back to sleep. The hall you cross for the fourth room is not the hall you
 * crossed for the first. */
static void set_hall_count(void)
{
    unsigned char i, want = HALL_BASE;
    for (i = 0; i < ROOMS_PER_LEVEL; i++) if (slot_done[i]) want++;
    if (want > MAX_HALL) want = MAX_HALL;
    for (i = want; i < MAX_HALL; i++) h_live[i] = 0;
}

static void enter_map(void)
{
    mode = MODE_MAP;
    load_board(map_layout, MAP_W, MAP_H, MAP_X, MAP_Y);
    cut_notches();
    set_hall_count();

    /* Back beside the entrance we went in by, never on it: standing on it would
     * re-enter the room instantly, and after a death in an unfinished room that is
     * a loop with no way out. */
    wx = MAP_START_X;
    wy = MAP_START_Y;
    if (slot_entered < ROOMS_PER_LEVEL) {
        if (ret_x + 1 < MAP_W && !blocked(ret_x + 1, ret_y)) { wx = ret_x + 1; wy = ret_y; }
        else if (ret_x && !blocked(ret_x - 1, ret_y))        { wx = ret_x - 1; wy = ret_y; }
        else if (ret_y + 1 < MAP_H && !blocked(ret_x, ret_y + 1)) { wx = ret_x; wy = ret_y + 1; }
        else if (ret_y && !blocked(ret_x, ret_y - 1))        { wx = ret_x; wy = ret_y - 1; }
    }
    face_dx = 0; face_dy = -1;
    dawdle = 0;

    vfill(' '); vcmd(VCMD_CLEAR);
    set_play_rows();
    draw_board();
    draw_hud();
}

static void enter_room(unsigned char slot, unsigned char which)
{
    mode = MODE_ROOM;
    slot_entered = slot;
    theme = room_of_slot[slot];
    load_board(room_art[theme], ROOM_W, ROOM_H, ROOM_X, ROOM_Y);
    exits_of(theme, rx_side, rx_x, rx_y);

    /* In at the doorway on the same side as the hall entrance we used, facing in. */
    step_inward(rx_x[which], rx_y[which]);
    exit_used = which;

    have_treasure = 0;
    left_room = 0;
    dawdle = 0;

    vfill(' '); vcmd(VCMD_CLEAR);
    set_play_rows();
    draw_board();
    draw_hud();
}

/* Which room's entrance is at this hall cell, or 0xFF. */
static unsigned char door_at(unsigned char rx, unsigned char ry)
{
    const unsigned char t = grid[ry][rx];
    return (t >= T_DOOR0) ? (unsigned char)(t - T_DOOR0) : 0xFF;
}

/* ...and which of that slot's two entrances it is. */
static unsigned char notch_index(unsigned char rx, unsigned char ry, unsigned char slot)
{
    return (sl_x[slot][1] == rx && sl_y[slot][1] == ry) ? 1 : 0;
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
     * while still aiming into it. The pip drawn each tick is what makes that
     * legible instead of a guess. */
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

/* The arrow and the monster it hit both come off the board, and the body goes in the
 * cell they shared. */
static void kill_monster(unsigned char i)
{
    m_live[i] = 0;
    grid[a_y][a_x] = T_CORPSE;       /* the body stays, and stays lethal */
    a_live = 0;
    /* Draw it now. Nothing else will: the monster is off the live list and so is the
     * arrow, and step() only restores cells belonging to things still alive. Without
     * this the body sits in the grid -- lethal, and killing you -- while the screen
     * still shows an arrow or a monster in that cell. */
    restore(a_x, a_y);
    /* The original's rule: monsters pay nothing until the treasure is yours. Clearing
     * the room first is the safe play and scores you nothing for it. */
    if (have_treasure) {
        score += (unsigned int)SCORE_MONSTER * level;
        cue(SND_KILL);
        draw_hud();
    }
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

        for (i = 0; i < MAX_MON; i++)
            if (m_live[i] && m_x[i] == a_x && m_y[i] == a_y) { kill_monster(i); return; }

        /* Hallmonsters cannot be shot. The arrow just stops on one. */
        for (i = 0; i < MAX_HALL; i++) {
            if (h_live[i] && h_x[i] == a_x && h_y[i] == a_y) {
                a_live = 0; restore(a_x, a_y); return;
            }
        }
    }
}

/* ---- pursuit ------------------------------------------------------------
 * A greedy step toward Winky, closing the larger axis first, with two fallbacks.
 * Slower than he is and easily out-manoeuvred in the open, which is the point: the
 * danger is being cornered, not outrun.
 *
 * Room monsters and Hallmonsters share it. What differs is what stops them --
 * `solid`, below -- and that is the whole difference between a monster and a clock.
 */
static unsigned char chase_self;    /* the monster taking this step, 0xFF for none */

static unsigned char chase_blocked(unsigned char rx, unsigned char ry,
                                   unsigned char through_walls,
                                   unsigned char avoid_bodies)
{
    /* A room monster will not walk over a body: the room closes in on them as much
     * as on you, and a corpse it cannot cross is a corpse it has to go round. It has
     * to be tested HERE and not vetoed after the fact -- a step chosen and then
     * refused leaves the monster standing still, which is exactly what made them sit
     * next to their own dead waiting to be shot.
     *
     * Nor will it walk onto another one. They all chase the same target, so without
     * this they converge into the same cell and draw as a single glyph -- you cannot
     * see how many are coming, and one arrow appears to kill two. Crowding is the
     * honest behaviour and it makes a doorway worth holding. */
    if (avoid_bodies) {
        unsigned char i;
        if (grid[ry][rx] == T_CORPSE) return 1;
        for (i = 0; i < MAX_MON; i++)
            if (i != chase_self && m_live[i] && m_x[i] == rx && m_y[i] == ry) return 1;
    }
    if (through_walls) return 0;
    return pursuit_blocked(rx, ry);
}

static void chase(unsigned char *px, unsigned char *py,
                  unsigned char through_walls, unsigned char avoid_bodies)
{
    const unsigned char cx = *px, cy = *py;
    unsigned char nx = cx, ny = cy, adx, ady;
    const signed char dx = (wx > cx) ? 1 : (wx < cx) ? -1 : 0;
    const signed char dy = (wy > cy) ? 1 : (wy < cy) ? -1 : 0;

    adx = (unsigned char)(wx > cx ? wx - cx : cx - wx);
    ady = (unsigned char)(wy > cy ? wy - cy : cy - wy);

#define OPEN(X, Y) ((X) < gw && (Y) < gh && \
                    !chase_blocked((X), (Y), through_walls, avoid_bodies))

    if (dx && (!dy || adx >= ady)) {
        if (OPEN((unsigned char)(cx + dx), cy)) nx = (unsigned char)(cx + dx);
        else if (dy && OPEN(cx, (unsigned char)(cy + dy))) ny = (unsigned char)(cy + dy);
    } else if (dy) {
        if (OPEN(cx, (unsigned char)(cy + dy))) ny = (unsigned char)(cy + dy);
        else if (dx && OPEN((unsigned char)(cx + dx), cy)) nx = (unsigned char)(cx + dx);
    }

    /* Both ways forward blocked. Sidestep across the direction we wanted, which
     * walks round a body or a wall corner instead of staring at it. Deterministic, so
     * a monster commits to going round one side rather than dithering. */
    if (nx == cx && ny == cy) {
        if (dx) {
            if (OPEN(cx, (unsigned char)(cy - 1)))      ny = (unsigned char)(cy - 1);
            else if (OPEN(cx, (unsigned char)(cy + 1))) ny = (unsigned char)(cy + 1);
        } else if (dy) {
            if (OPEN((unsigned char)(cx - 1), cy))      nx = (unsigned char)(cx - 1);
            else if (OPEN((unsigned char)(cx + 1), cy)) nx = (unsigned char)(cx + 1);
        }
    }

    /* Boxed in on all four sides -- possible once the room fills with bodies. Take
     * any open cell rather than freeze. In the hall this is also what keeps a
     * Hallmonster patrolling when the greedy path dead-ends. */
    if (nx == cx && ny == cy) {
        const unsigned char dir = rndn(4);
        const unsigned char tx = (unsigned char)(cx + ((dir == 0) ? 1 : (dir == 1) ? -1 : 0));
        const unsigned char ty = (unsigned char)(cy + ((dir == 2) ? 1 : (dir == 3) ? -1 : 0));
        if (OPEN(tx, ty)) { nx = tx; ny = ty; }
    }
#undef OPEN

    *px = nx;
    *py = ny;
}

static void monsters_advance(void)
{
    unsigned char i;
    for (i = 0; i < MAX_MON; i++) {
        if (!m_live[i]) continue;
        chase_self = i;
        chase(&m_x[i], &m_y[i], 0, 1);      /* walls, bodies and each other stop them */

        /* A monster that walks INTO the arrow dies on it. Without this the two swap
         * cells and the shot goes straight through: the arrow advances past the
         * monster's old cell earlier in this same tick, then the monster steps into
         * the arrow's new one and nothing looks again. It only happens on the ticks
         * monsters actually move, which is why it read as random. */
        if (a_live && m_x[i] == a_x && m_y[i] == a_y) { kill_monster(i); continue; }

        if (m_x[i] == wx && m_y[i] == wy) dead = 1;
    }
}

/* Same swap, and a Hallmonster still cannot be shot -- the arrow just stops on it. */
static void hall_arrow_check(void)
{
    unsigned char i;
    if (!a_live) return;
    for (i = 0; i < MAX_HALL; i++)
        if (h_live[i] && h_x[i] == a_x && h_y[i] == a_y) {
            a_live = 0;
            restore(a_x, a_y);
            return;
        }
}

/* In the hall they walk it like anyone else. Inside a room they walk THROUGH THE
 * WALLS -- straight at you, round nothing, stuck on nothing. There is no cornering
 * one and no losing one behind a wall; the only answer is to leave, which is what
 * makes it a clock rather than an enemy. */
static void hall_advance(void)
{
    const unsigned char phases = (mode == MODE_ROOM);
    unsigned char i;
    chase_self = 0xFF;
    for (i = 0; i < MAX_HALL; i++) {
        if (!h_live[i]) continue;
        chase(&h_x[i], &h_y[i], phases, 0);
        if (h_x[i] == wx && h_y[i] == wy) dead = 1;
    }
}

/* Dawdle in a room and one comes through a doorway after you. It cannot be shot,
 * blocked or outrun for long; there is nothing to do about it except leave. This
 * is the clock that stops Venture being a leisurely looting exercise. */
static void hall_intrude(void)
{
    unsigned char rx, ry;

    if (h_live[0]) return;
    for (ry = 0; ry < ROOM_H; ry++)
        for (rx = 0; rx < ROOM_W; rx++)
            if (grid[ry][rx] == T_EXIT) {
                h_live[0] = 1; h_x[0] = rx; h_y[0] = ry;
                cue(SND_HALL);
                return;
            }
}

/* ---- the facing pip ---------------------------------------------------- */
static void draw_facing(void)
{
    const unsigned char tx = (unsigned char)(wx + face_dx);
    const unsigned char ty = (unsigned char)(wy + face_dy);

    f_live = 0;
    if (mode != MODE_ROOM) return;                 /* the bow is for rooms */
    if (tx >= gw || ty >= gh) return;
    if (grid[ty][tx] != T_FLOOR) return;           /* never over a wall or a body */
    if (lethal(tx, ty)) return;                    /* let the danger show instead */

    f_live = 1; f_x = tx; f_y = ty;
    put_at(tx, ty,
           face_dy < 0 ? G_FACE_U : face_dy > 0 ? G_FACE_D :
           face_dx < 0 ? G_FACE_L : G_FACE_R, A_FACE);
}

/* ---- one simulation step ---------------------------------------------- */
static void step(unsigned char ks)
{
    unsigned char i, slot;

    /* Erase everything that can move, from the grid underneath it. */
    restore(wx, wy);
    if (f_live) restore(f_x, f_y);
    if (a_live) restore(a_x, a_y);
    for (i = 0; i < MAX_MON; i++)  if (m_live[i]) restore(m_x[i], m_y[i]);
    for (i = 0; i < MAX_HALL; i++) if (h_live[i]) restore(h_x[i], h_y[i]);
    f_live = 0;

    tick_count++;
    cue_tick();
    winky_move(ks);

    if (mode == MODE_MAP) {
        /* Walking onto an entrance is the commitment. enter_room() rebuilds the
         * whole board underneath us, so this step ends here. */
        slot = door_at(wx, wy);
        if (slot != 0xFF && !slot_done[slot]) {
            ret_x = wx; ret_y = wy;      /* where to put us back if we die in there */
            enter_room(slot, notch_index(wx, wy, slot));
            return;
        }
        if (lethal(wx, wy)) { dead = 1; return; }
        if (!(tick_count % HALL_EVERY)) hall_advance();
    } else {
        if (grid[wy][wx] == T_TREAS) {
            grid[wy][wx] = T_FLOOR;
            have_treasure = 1;
            treas_got |= (unsigned char)(1 << theme);
            score += (unsigned int)SCORE_TREASURE * level;
            cue(SND_TREASURE);
            draw_hud();
        }
        /* Either doorway is a way out, and only with the goods. Which one decides
         * where in the hall we reappear: leave by the north door and you step out at
         * the north of the room's block. */
        if (have_treasure && grid[wy][wx] == T_EXIT) {
            exit_used = (rx_x[1] == wx && rx_y[1] == wy) ? 1 : 0;
            ret_x = sl_x[slot_entered][exit_used];
            ret_y = sl_y[slot_entered][exit_used];
            left_room = 1;
            return;
        }
        if (lethal(wx, wy)) { dead = 1; return; }

        if (ks & KS_FIRE) fire();
        arrow_advance();
        if (!(tick_count % MON_EVERY)) monsters_advance();
        if (++dawdle > HALL_ROOM_TICKS) hall_intrude();
        if (!(tick_count % HALL_IN_EVERY)) { hall_advance(); hall_arrow_check(); }
    }

    if (lethal(wx, wy)) dead = 1;   /* something may have stepped onto Winky */
    if (dead) return;

    /* Redraw in an order that keeps Winky visible, and puts anything dangerous
     * over the facing pip: he is the thing the player is tracking, so he wins any
     * overlap, and a pip must never hide a monster. */
    draw_facing();
    for (i = 0; i < MAX_MON; i++)
        if (m_live[i]) put_at(m_x[i], m_y[i], theme_monster[theme], A_MON);
    for (i = 0; i < MAX_HALL; i++)
        if (h_live[i]) put_at(h_x[i], h_y[i], G_HALLMON, a_hall);
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

/* ---- full-screen interludes -------------------------------------------- */
static void wait_key(void)
{
    while (INCH_NB() >= 0) { }       /* drain, so a stale byte cannot skip it */
    while (INCH_NB() < 0) { }
}

static void banner(const char *line)
{
    vfill(' ');
    vcmd(VCMD_CLEAR);
    put_str(33, 11, line, A_HUD);
    wait_key();
}

/* The arcade's level-start screen: a roster of every treasure in the game, each
 * slot a '?' until you have taken that one, then its glyph in its own colour. It
 * is the only long-run progress the game shows, and it is the reason to go back
 * into a room type you have already survived. */
static void roster_screen(void)
{
    unsigned char i;

    vfill(' ');
    vcmd(VCMD_CLEAR);
    put_str(22, 9, "TREASURES:", A_HUD);
    for (i = 0; i < THEMES; i++) {
        vaddr((unsigned int)9 * SCR_W + 34 + i * 3);
        if (treas_got & (1 << i)) {
            vattr(theme_attr[i]);                 /* each treasure in its own colour */
            vputc(theme_treasure[i]);
        } else {
            vattr(A_TEXT);
            vputc('?');
        }
    }
    put_str(31, 13, "PLAYER 1 GET READY", A_HUD);
    wait_key();
}

/* ...and its between-levels tally. */
static void bonus_screen(unsigned int earned, unsigned char mult, unsigned int total)
{
    vfill(' ');
    vcmd(VCMD_CLEAR);
    put_str(22, 9,  "SCORE THIS LEVEL", A_HUD);
    put_num(46, 9,  earned, 6, A_HUD);
    put_str(22, 11, "BONUS MULTIPLIER", A_HUD);
    put_str(46, 11, "X", A_HUD);
    put_num(47, 11, mult, 1, A_HUD);
    put_str(22, 13, "TOTAL BONUS", A_HUD);
    put_num(46, 13, total, 6, A_HUD);
    wait_key();
}

/* Deal this level's four rooms from the six themes, rotating with the level so a
 * run of twelve room-visits is not four layouts seen three times each, and adopt
 * the level's palette. */
static void start_level(void)
{
    const unsigned char p = (unsigned char)((level - 1) % LEVELS);
    unsigned char i;

    a_wall = lvl_wall[p];
    a_room = lvl_room[p];
    a_hall = lvl_hall[p];

    for (i = 0; i < ROOMS_PER_LEVEL; i++) {
        unsigned char ex[2], ey[2];
        room_of_slot[i] =
            (unsigned char)(((level - 1) * ROOMS_PER_LEVEL + i) % THEMES);
        slot_done[i] = 0;
        /* Take the sides of this room's doorways now, so the hall can cut its
         * entrances to match. A slot holds a different room each level, so the hall's
         * entrances move with it. */
        exits_of(room_of_slot[i], sl_side[i], ex, ey);
    }
    level_done = 0;
    level_base = score;
    slot_entered = 0xFF;
}

/* Multiply without a 32-bit runtime, and stop at 65535 rather than wrap -- a score
 * that rolls over looks exactly like a bug. */
static unsigned int scaled(unsigned int v, unsigned char n)
{
    unsigned int acc = 0;
    while (n--) {
        if ((unsigned int)(acc + v) < acc) return 0xFFFF;
        acc += v;
    }
    return acc;
}

int main(void)
{
    unsigned int last, now, earned, total;
    unsigned char ks, catchup, i, mult;
    int k;

    rng = rng_seed();
    if (!rng) rng = 0xACE1;

    vhidecur();
    banner("V E N T U R E");

    start_level();
    roster_screen();
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
                earned = score - level_base;
                mult = BONUS_MULT(level, lives);
                total = scaled(earned, mult);
                score = ((unsigned int)(score + total) < score) ? 0xFFFF
                                                               : (score + total);
                bonus_screen(earned, mult, total);

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
                roster_screen();
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
