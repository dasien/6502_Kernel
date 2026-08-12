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
 * is how the arcade draws its dungeon floor -- and, as there, the rooms are different
 * shapes and sizes from each other and the whole floor changes between levels. Four
 * identical rectangles reused three times reads as one room drawn four times.
 *
 * Every block keeps a one-tile moat, so an entrance can be cut into any of its faces.
 * The entrances are not in the picture: where they go depends on which room the slot
 * is holding this level -- see cut_notches(). */

static const char *const map_layout[LEVELS][MAP_H] = {
    {   /* level 1 */
        "##############################",
        "#...........................h#",
        "#.AAAAAAA.......BBBBBBBBBBB..#",
        "#hAaaaaaA.......BbbbbbbbbbB..#",
        "#.AAAAAAA.......BBBBBBBBBBB..#",
        "#............h...............#",
        "#.CCCCCCCCCCC.......DDDDDDD..#",
        "#hCcccccccccC.......DdddddD..#",
        "#.CCCCCCCCCCC.......DDDDDDD..#",
        "#...........h...........h....#",
        "##############################",
    },
    {   /* level 2 */
        "##############################",
        "#...........................h#",
        "#.AAAAAAA....BBBBBBBBB.......#",
        "#.AaaaaaA....BbbbbbbbB..DDDD.#",
        "#.AaaaaaA....BBBBBBBBB..DddD.#",
        "#.AaaaaaA.h.............DddDh#",
        "#.AaaaaaA....CCCCCCCCC..DddD.#",
        "#.AaaaaaA....CcccccccC..DDDD.#",
        "#.AAAAAAA.h..CCCCCCCCC......h#",
        "#...........h................#",
        "##############################",
    },
    {   /* level 3 */
        "##############################",
        "#...........................h#",
        "#.AAAAAAA..BBBBBBBBB..CCCCCC.#",
        "#hAaaaaaA..BbbbbbbbB..CccccC.#",
        "#.AAAAAAA..BBBBBBBBB..CccccCh#",
        "#.....................CccccC.#",
        "#....DDDDDDDDDDD......CCCCCC.#",
        "#....DdddddddddD.............#",
        "#....DDDDDDDDDDD............h#",
        "#h..................h........#",
        "##############################",
    },
};




/* Six themed rooms; a level deals four of them. One table rather than six named
 * arrays and a pointer list -- cc65 will not initialise a pointer table from
 * other arrays' names, and the table is what the code wants anyway. */
static const char *const room_art[THEMES][ROOM_H] = {
    {   /* 0 SERPENT */
        "###############+##############",
        "#.........#..................#",
        "#.........#..............m...#",
        "#....m....#..................#",
        "#.........#.........m........#",
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
        "#............................#",
        "#.......##############.......#",
        "#.......#............#.......#",
        "+..........m..*..............#",
        "#.......#............#.......#",
        "#.......##############.......#",
        "#.........................m..#",
        "#.......m....................#",
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
        "#.............m..............#",
        "+.........................m..#",
        "#...#########....#########...#",
        "#...#########....#########...#",
        "#...#########.*..#########...#",
        "#...#########....#########...#",
        "#...#########....#########...#",
        "#............................+",
        "#.m..........................#",
        "##############################",
    },
    {   /* 4 SKELETON */
        "####+#########################",
        "#...............m............#",
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

static unsigned char hall_of_level;                 /* which floor plan is up */
static unsigned char sl_side[ROOMS_PER_LEVEL][2];   /* each slot's two door sides */
static unsigned char sl_x[ROOMS_PER_LEVEL][2];      /* and where they got cut */
static unsigned char sl_y[ROOMS_PER_LEVEL][2];

static unsigned char rx_side[2], rx_x[2], rx_y[2];  /* the current room's doorways */
static unsigned char exit_used;                     /* which one we walked out of */

unsigned char mode;                                         /* MODE_MAP / MODE_ROOM */
static unsigned char theme;                /* which of the six rooms we are in */
static unsigned char room_of_slot[ROOMS_PER_LEVEL];
static unsigned char slot_done[ROOMS_PER_LEVEL];
static unsigned char slot_entered = 0xFF;  /* the room we last went into */
static unsigned char ret_x, ret_y;         /* the hall cell we went in from */

/* ---- entities ----------------------------------------------------------
 * Deliberately NOT static, and it costs nothing: dropping the keyword exports the
 * symbol to the linker's label file without changing a byte of the binary. The test
 * harness reads these addresses to learn where things ARE.
 *
 * It used to find them by scanning the screen, which stopped working the moment the
 * movers began sliding between cells: a sprite in flight is drawn between two of
 * them and its position does not carry direction -- three sixteenths right and
 * thirteen sixteenths left land on the same pixel -- so the cell it occupies is not
 * recoverable from the picture. Asking the game is not a workaround for that; it is
 * the right question. The screen is still what the drawing tests assert on. */
unsigned char wx, wy;                    /* Winky, in board coordinates */
signed char          face_dx, face_dy;   /* last direction held; arrows use it */

unsigned char f_live, f_x, f_y;          /* the facing pip */

unsigned char m_live[MAX_MON], m_x[MAX_MON], m_y[MAX_MON];
static unsigned char m_hx[MAX_MON], m_hy[MAX_MON];   /* the post each one guards */
static unsigned char m_ox[MAX_MON], m_oy[MAX_MON];   /* the cell each came from */
unsigned char h_live[MAX_HALL], h_x[MAX_HALL], h_y[MAX_HALL];
static unsigned char h_ox[MAX_HALL], h_oy[MAX_HALL];

unsigned char a_live;                    /* one arrow in flight, as the original */
unsigned char a_x, a_y;

/* Where every mover was at its last step, and when the slower classes took it.
 * Drawing slides between the previous cell and the current one, so a mover crosses
 * the cell over the whole span between its steps instead of appearing in the next
 * one. The simulation is untouched: it still steps whole cells, and every
 * collision, lethal() and tile test still asks the grid. Only the picture moves
 * continuously, which is the only place the jump was ever visible. */
static unsigned char pw_x, pw_y, pf_x0, pf_y0, pa_x, pa_y;
static unsigned char pm_x[MAX_MON], pm_y[MAX_MON];
static unsigned char ph_x[MAX_HALL], ph_y[MAX_HALL];
static unsigned int  mon_at, hall_at;
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
/* VENTURE's own glyphs, uploaded into the chip's font RAM at startup.
 *
 * The shapes used to be whichever CP437 codepoints looked closest -- a section
 * sign for a serpent, a theta for a cyclops. They were chosen by rendering the
 * ROM and picking, which is as far as a fixed font lets you go. With the font in
 * RAM the shapes are ours, so these are drawn rather than found.
 *
 * Each glyph is 16 scanlines of 8 pixels, and the playfield is double-size rows,
 * so every pixel here is a 2x2 block on screen. The art beside each row is the
 * source of truth -- edit the picture, then the byte.
 *
 * The codes are the ones the game already used, so nothing else has to change:
 * the tile tables, the tests and the draw calls all still name the same glyph. */
static const unsigned char FONT_ART[] = {
    /* G_WINKY  0x01 */ 0x01,
        0x00,   /* |        | */
        0x3C,   /* |  ####  | */
        0x7E,   /* | ###### | */
        0xFF,   /* |########| */
        0xDB,   /* |## ## ##| */
        0xDB,   /* |## ## ##| */
        0xFF,   /* |########| */
        0xFF,   /* |########| */
        0xFF,   /* |########| */
        0xBD,   /* |# #### #| */
        0xC3,   /* |##    ##| */
        0xFF,   /* |########| */
        0x7E,   /* | ###### | */
        0x3C,   /* |  ####  | */
        0x00,   /* |        | */
        0x00,   /* |        | */
    /* G_CORPSE  0xB0 */ 0xB0,
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x3C,   /* |  ####  | */
        0x42,   /* | #    # | */
        0xA5,   /* |# #  # #| */
        0x81,   /* |#      #| */
        0x42,   /* | #    # | */
        0x3C,   /* |  ####  | */
        0x66,   /* | ##  ## | */
        0xA5,   /* |# #  # #| */
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x00,   /* |        | */
    /* G_DOORWAY  0xF0 */ 0xF0,
        0x00,   /* |        | */
        0xC3,   /* |##    ##| */
        0xC3,   /* |##    ##| */
        0xC3,   /* |##    ##| */
        0xC3,   /* |##    ##| */
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x00,   /* |        | */
        0xC3,   /* |##    ##| */
        0xC3,   /* |##    ##| */
        0xC3,   /* |##    ##| */
        0xC3,   /* |##    ##| */
        0x00,   /* |        | */
    /* M_SERPENT  0x15 */ 0x15,
        0x00,   /* |        | */
        0x38,   /* |  ###   | */
        0x7C,   /* | #####  | */
        0xD6,   /* |## # ## | */
        0xFE,   /* |####### | */
        0x7E,   /* | ###### | */
        0x3C,   /* |  ####  | */
        0x1C,   /* |   ###  | */
        0x3C,   /* |  ####  | */
        0x7C,   /* | #####  | */
        0xDC,   /* |## ###  | */
        0xCC,   /* |##  ##  | */
        0xCC,   /* |##  ##  | */
        0xF8,   /* |#####   | */
        0x70,   /* | ###    | */
        0x00,   /* |        | */
    /* M_CYCLOPS  0xE9 */ 0xE9,
        0x3C,   /* |  ####  | */
        0x7E,   /* | ###### | */
        0xFF,   /* |########| */
        0xC3,   /* |##    ##| */
        0x99,   /* |#  ##  #| */
        0xBD,   /* |# #### #| */
        0x99,   /* |#  ##  #| */
        0xC3,   /* |##    ##| */
        0xFF,   /* |########| */
        0x7E,   /* | ###### | */
        0x3C,   /* |  ####  | */
        0x24,   /* |  #  #  | */
        0x24,   /* |  #  #  | */
        0x66,   /* | ##  ## | */
        0x00,   /* |        | */
        0x00,   /* |        | */
    /* M_SPIDER  0x0F */ 0x0F,
        0x00,   /* |        | */
        0x99,   /* |#  ##  #| */
        0x5A,   /* | # ## # | */
        0x3C,   /* |  ####  | */
        0xBD,   /* |# #### #| */
        0xDB,   /* |## ## ##| */
        0xFF,   /* |########| */
        0xDB,   /* |## ## ##| */
        0xBD,   /* |# #### #| */
        0x3C,   /* |  ####  | */
        0x5A,   /* | # ## # | */
        0x99,   /* |#  ##  #| */
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x00,   /* |        | */
    /* M_GOAT  0xEA */ 0xEA,
        0xC3,   /* |##    ##| */
        0xC3,   /* |##    ##| */
        0x42,   /* | #    # | */
        0x3C,   /* |  ####  | */
        0x7E,   /* | ###### | */
        0xDB,   /* |## ## ##| */
        0xFF,   /* |########| */
        0xFF,   /* |########| */
        0xFF,   /* |########| */
        0x7E,   /* | ###### | */
        0x3C,   /* |  ####  | */
        0x24,   /* |  #  #  | */
        0x24,   /* |  #  #  | */
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x00,   /* |        | */
    /* M_SKELETON  0x9D */ 0x9D,
        0x3C,   /* |  ####  | */
        0x7E,   /* | ###### | */
        0xDB,   /* |## ## ##| */
        0xFF,   /* |########| */
        0xBD,   /* |# #### #| */
        0x3C,   /* |  ####  | */
        0x18,   /* |   ##   | */
        0x7E,   /* | ###### | */
        0xFF,   /* |########| */
        0xBD,   /* |# #### #| */
        0xBD,   /* |# #### #| */
        0x3C,   /* |  ####  | */
        0x24,   /* |  #  #  | */
        0x66,   /* | ##  ## | */
        0x00,   /* |        | */
        0x00,   /* |        | */
    /* M_WRAITH  0xE8 */ 0xE8,
        0x3C,   /* |  ####  | */
        0x7E,   /* | ###### | */
        0xFF,   /* |########| */
        0xC3,   /* |##    ##| */
        0xC3,   /* |##    ##| */
        0xFF,   /* |########| */
        0xFF,   /* |########| */
        0xFF,   /* |########| */
        0xFF,   /* |########| */
        0xFE,   /* |####### | */
        0xFC,   /* |######  | */
        0xF8,   /* |#####   | */
        0xE0,   /* |###     | */
        0x40,   /* | #      | */
        0x00,   /* |        | */
        0x00,   /* |        | */
    /* T_APPLES  0x05 */ 0x05,
        0x00,   /* |        | */
        0x18,   /* |   ##   | */
        0x3C,   /* |  ####  | */
        0x7E,   /* | ###### | */
        0xFF,   /* |########| */
        0xFF,   /* |########| */
        0xFF,   /* |########| */
        0xFF,   /* |########| */
        0xFF,   /* |########| */
        0x7E,   /* | ###### | */
        0x3C,   /* |  ####  | */
        0x18,   /* |   ##   | */
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x00,   /* |        | */
    /* T_JEWEL  0x04 */ 0x04,
        0x00,   /* |        | */
        0x18,   /* |   ##   | */
        0x3C,   /* |  ####  | */
        0x66,   /* | ##  ## | */
        0xC3,   /* |##    ##| */
        0xC3,   /* |##    ##| */
        0x66,   /* | ##  ## | */
        0x3C,   /* |  ####  | */
        0x18,   /* |   ##   | */
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x00,   /* |        | */
    /* T_RING  0x09 */ 0x09,
        0x00,   /* |        | */
        0x18,   /* |   ##   | */
        0x3C,   /* |  ####  | */
        0x24,   /* |  #  #  | */
        0x24,   /* |  #  #  | */
        0x3C,   /* |  ####  | */
        0x7E,   /* | ###### | */
        0xC3,   /* |##    ##| */
        0x81,   /* |#      #| */
        0x81,   /* |#      #| */
        0x81,   /* |#      #| */
        0x42,   /* | #    # | */
        0x3C,   /* |  ####  | */
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x00,   /* |        | */
    /* T_INGOT  0xFE */ 0xFE,
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x3C,   /* |  ####  | */
        0x7E,   /* | ###### | */
        0xFF,   /* |########| */
        0xFF,   /* |########| */
        0xFF,   /* |########| */
        0xFF,   /* |########| */
        0x7E,   /* | ###### | */
        0x3C,   /* |  ####  | */
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x00,   /* |        | */
    /* T_CHEST  0x0A */ 0x0A,
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x3C,   /* |  ####  | */
        0x7E,   /* | ###### | */
        0xFF,   /* |########| */
        0xFF,   /* |########| */
        0xDB,   /* |## ## ##| */
        0xFF,   /* |########| */
        0xFF,   /* |########| */
        0xFF,   /* |########| */
        0xFF,   /* |########| */
        0x7E,   /* | ###### | */
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x00,   /* |        | */
    /* T_AMULET  0x03 */ 0x03,
        0x00,   /* |        | */
        0x18,   /* |   ##   | */
        0x18,   /* |   ##   | */
        0x3C,   /* |  ####  | */
        0x66,   /* | ##  ## | */
        0xC3,   /* |##    ##| */
        0xDB,   /* |## ## ##| */
        0xDB,   /* |## ## ##| */
        0xC3,   /* |##    ##| */
        0x66,   /* | ##  ## | */
        0x3C,   /* |  ####  | */
        0x18,   /* |   ##   | */
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x00,   /* |        | */
        0x00,   /* |        | */
    0x00        /* terminator: a code of 0 ends the table */
};


/* Upload FONT_ART and switch the chip to the RAM font.
 *
 * Font RAM is seeded from the CP437 ROM, so writing only our glyphs leaves the other
 * 240-odd alone -- the HUD text, the score digits and the wall block all still render
 * exactly as before. */
static void load_font(void)
{
    const unsigned char *p = FONT_ART;
    unsigned char row;

    while (*p) {
        vfontaddr((unsigned int)*p++ * 16);
        for (row = 0; row < 16; row++) vfontput(*p++);
    }
    vcmd(VCMD_FONTRAM);
}

/* Hand the chip's font back before leaving.
 *
 * Measured, not assumed: DOS's warm start clears the screen, and a clear already
 * resets the font -- so the prompt comes back readable with or without this. It is
 * here so that staying readable is not a property of what DOS happens to do on
 * re-entry. The font is global state VENTURE switched on; VENTURE switches it off. */
static void restore_font(void)
{
    vcmd(VCMD_FONTROM);
}

/* Clear the screen and put our font back.
 *
 * A chip-side clear resets the font to the ROM, the rows to single size and the
 * scroll region to the whole screen -- deliberately, so that however badly a program
 * exits it cannot strand the shell with an unreadable screen. The price is that a
 * program which wants any of them has to re-assert after every clear. The playfield
 * already re-flagged its double rows for exactly this reason; the font is the same
 * contract, and missing it left the whole dungeon drawn in CP437 again. */
static void clear_screen(void)
{
    vfill(' ');
    vcmd(VCMD_CLEAR);
    vcmd(VCMD_FONTRAM);
}

/* How far through its step a class is, in sixteenths of a cell. One divide per
 * class per frame, not per mover -- every room monster shares a cadence. Clamped,
 * because a class can overrun its span while a room is being entered. */
static unsigned char slide(unsigned int elapsed, unsigned int span)
{
    unsigned int o;
    if (!span) return 16;
    o = (elapsed << 4) / span;
    return (unsigned char)(o > 16 ? 16 : o);
}

/* A cell's nominal-pixel position, `off` sixteenths of the way from prev to cur.
 * A step is one cell, so anything longer is a teleport -- a room change, a respawn
 * -- and snaps, or the mover would sail across the playfield. */
static unsigned int glide_x(unsigned char prev, unsigned char cur, unsigned char off)
{
    unsigned int base = (unsigned int)(gx0 + prev) << 4;
    if (cur == prev) return base;
    if (cur == (unsigned char)(prev + 1)) return base + off;
    if (prev == (unsigned char)(cur + 1)) return base - off;
    return (unsigned int)(gx0 + cur) << 4;
}

/* Twice the offset per sixteenth: a playfield row is two character rows tall. */
static unsigned int glide_y(unsigned char prev, unsigned char cur, unsigned char off)
{
    unsigned int base = (unsigned int)(PLAY_ROW0 + ((gy0 + prev) << 1)) << 4;
    if (cur == prev) return base;
    if (cur == (unsigned char)(prev + 1)) return base + (off << 1);
    if (prev == (unsigned char)(cur + 1)) return base - (off << 1);
    return (unsigned int)(PLAY_ROW0 + ((gy0 + cur) << 1)) << 4;
}

/* mag: draw the pattern at 2x, which is what makes a sprite the size of a
   double-row cell. Off gives a half-size marker -- see the facing pip. */
static void spr_nom(unsigned char slot, unsigned int nx, unsigned int ny,
                    unsigned char ch, unsigned char attr, unsigned char mag)
{
    volatile unsigned char *r = SPRITES + (unsigned int)slot * SPR_STRIDE;
    r[0] = (unsigned char)nx;
    r[1] = (unsigned char)(((nx >> 8) & 0x03) | mag);
    r[2] = (unsigned char)ny;
    r[3] = (unsigned char)(((ny >> 8) & 0x03) | mag | SPR_ENABLE);
    r[4] = ch;
    r[5] = attr;
}

static void spr_off(unsigned char slot)
{
    SPRITES[(unsigned int)slot * SPR_STRIDE + 3] = 0;   /* clears the enable bit */
}

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
                        m_live[nm] = 1; m_x[nm] = rx; m_y[nm] = ry;
                        m_hx[nm] = rx;  m_hy[nm] = ry;   /* where the art put it */
                        m_ox[nm] = 0xFF; m_oy[nm] = 0xFF;
                        nm++;
                    }
                    break;
                case 'h':
                    grid[ry][rx] = T_FLOOR;
                    if (nh < MAX_HALL) {
                        h_live[nh] = 1; h_x[nh] = rx; h_y[nh] = ry;
                        h_ox[nh] = 0xFF; h_oy[nh] = 0xFF;
                        nh++;
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

/* Is the cell on the given side of (x,y) open hall? */
static unsigned char face_open(unsigned char x, unsigned char y, unsigned char side)
{
    switch (side) {
        case SIDE_N: return y && grid[y - 1][x] == T_FLOOR;
        case SIDE_S: return (unsigned char)(y + 1) < gh && grid[y + 1][x] == T_FLOOR;
        case SIDE_W: return x && grid[y][x - 1] == T_FLOOR;
        default:     return (unsigned char)(x + 1) < gw && grid[y][x + 1] == T_FLOOR;
    }
}

/* Cut each slot's two entrances into the faces that match its room's doorway sides.
 *
 * Found by scanning rather than by arithmetic, because the blocks are no longer all
 * the same rectangle: take the outline cells that have open hall on the wanted side
 * and pick whichever is nearest the block's middle. Cutting the first entrance turns
 * that cell into a door, so the second pass cannot pick it again.
 *
 * A side with no open face would leave a room unenterable; the layouts are checked
 * for that offline, against the exact theme-to-slot mapping each level uses. */
static void cut_notches(void)
{
    unsigned char s, k, x, y, tile, cx, cy;
    unsigned char minx, maxx, miny, maxy, bx, by, bd, d, side;

    for (s = 0; s < ROOMS_PER_LEVEL; s++) {
        tile = (unsigned char)(T_BLK0 + s);
        minx = MAP_W; maxx = 0; miny = MAP_H; maxy = 0;
        for (y = 0; y < MAP_H; y++)
            for (x = 0; x < MAP_W; x++)
                if (grid[y][x] == tile) {
                    if (x < minx) minx = x;
                    if (x > maxx) maxx = x;
                    if (y < miny) miny = y;
                    if (y > maxy) maxy = y;
                }
        if (minx > maxx) continue;
        cx = (unsigned char)((minx + maxx) >> 1);
        cy = (unsigned char)((miny + maxy) >> 1);

        for (k = 0; k < 2; k++) {
            side = sl_side[s][k];
            bd = 0xFF; bx = 0; by = 0;
            for (y = miny; y <= maxy; y++)
                for (x = minx; x <= maxx; x++) {
                    if (grid[y][x] != tile || !face_open(x, y, side)) continue;
                    d = (side == SIDE_N || side == SIDE_S)
                            ? (unsigned char)(x > cx ? x - cx : cx - x)
                            : (unsigned char)(y > cy ? y - cy : cy - y);
                    if (d < bd) { bd = d; bx = x; by = y; }
                }
            if (bd == 0xFF) continue;
            grid[by][bx] = (unsigned char)(T_DOOR0 + s);
            sl_x[s][k] = bx;
            sl_y[s][k] = by;
        }
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

static unsigned char taxi(unsigned char ax, unsigned char ay,
                          unsigned char bx, unsigned char by)
{
    return (unsigned char)((ax > bx ? ax - bx : bx - ax) +
                           (ay > by ? ay - by : by - ay));
}

/* Taxicab distance from the level-start square. */
static unsigned int post_dist(unsigned char x, unsigned char y)
{
    return (unsigned int)(x > MAP_START_X ? x - MAP_START_X : MAP_START_X - x)
         + (unsigned int)(y > MAP_START_Y ? y - MAP_START_Y : MAP_START_Y - y);
}

/* Order the posts so the ones woken first are clear of the square the player starts
 * a level on AND spread across the hall.
 *
 * They are numbered in the order they appear in the layout art, which is scan order
 * and says nothing about either. With one Hallmonster awake that did not matter.
 * With three it matters twice: level one's third post in scan order sits one cell
 * from the start at (14,5), so the level would have opened with one already touching
 * you -- and simply taking the three FURTHEST from the start instead put two of them
 * together at the west wall, straight across the first room's south approach, which
 * made that entrance close to unreachable.
 *
 * So: farthest-point ordering seeded with the start square. Each post in turn is the
 * one whose nearest neighbour -- among the start and everything already chosen -- is
 * as far off as possible. That keeps them off the player at level start and spread
 * over the hall rather than bunched in one corner, and it stays a rule rather than a
 * property of how the art happens to be typed. */
static void order_hall_posts(void)
{
    unsigned char n = 0, i, j, k, best, t;
    unsigned int bestd, d, dd;

    while (n < MAX_HALL && h_live[n]) n++;
    for (i = 0; i < n; i++) {
        best = i; bestd = 0;
        for (j = i; j < n; j++) {
            d = post_dist(h_x[j], h_y[j]);
            for (k = 0; k < i; k++) {
                dd = (unsigned int)(h_x[j] > h_x[k] ? h_x[j] - h_x[k] : h_x[k] - h_x[j])
                   + (unsigned int)(h_y[j] > h_y[k] ? h_y[j] - h_y[k] : h_y[k] - h_y[j]);
                if (dd < d) d = dd;
            }
            if (d > bestd) { bestd = d; best = j; }
        }
        if (best != i) {
            t = h_x[i];  h_x[i]  = h_x[best];  h_x[best]  = t;
            t = h_y[i];  h_y[i]  = h_y[best];  h_y[best]  = t;
            t = h_ox[i]; h_ox[i] = h_ox[best]; h_ox[best] = t;
            t = h_oy[i]; h_oy[i] = h_oy[best]; h_oy[best] = t;
        }
    }
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
    load_board(map_layout[hall_of_level], MAP_W, MAP_H, MAP_X, MAP_Y);
    cut_notches();
    order_hall_posts();
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

    clear_screen();
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

    clear_screen();
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
/* Where the current chase() is heading. Winky, usually -- but a room monster
   holding its ground aims at its post instead, and one that is already there aims at
   itself, which makes chase() fall through to its wander. */
static unsigned char tgt_x, tgt_y;
static unsigned char chase_self;    /* the room monster taking this step, 0xFF for none */
static unsigned char hall_self = 0xFF;  /* the Hallmonster taking this step */
static unsigned char back_x, back_y; /* the cell it came from, which it may not retake */

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
    /* One cell of memory stops a monster oscillating against a long wall. The room
       intruder walks THROUGH walls, so it has no wall to oscillate against -- and
       refusing to double back only made it sidestep whenever you slipped past it,
       which is precisely when it should be turning round and coming after you. */
    if (!through_walls && rx == back_x && ry == back_y) return 1;
    if (avoid_bodies) {
        unsigned char i;
        if (grid[ry][rx] == T_CORPSE) return 1;
        for (i = 0; i < MAX_MON; i++)
            if (i != chase_self && m_live[i] && m_x[i] == rx && m_y[i] == ry) return 1;
    }
    /* Nor do Hallmonsters share a cell. They all patrol the same hall and drift
       toward the same player, so without this they pile into one square and draw as a
       single figure -- you cannot see how many are coming, and the hall looks emptier
       than it is. Checked only while a Hallmonster is the one stepping, so it does not
       change how a room monster moves around the one that follows you in. */
    if (hall_self != 0xFF) {
        unsigned char k;
        for (k = 0; k < MAX_HALL; k++)
            if (k != hall_self && h_live[k] && h_x[k] == rx && h_y[k] == ry) return 1;
    }
    if (through_walls) return 0;
    return pursuit_blocked(rx, ry);
}

static void chase(unsigned char *px, unsigned char *py,
                  unsigned char through_walls, unsigned char avoid_bodies)
{
    const unsigned char cx = *px, cy = *py;
    unsigned char nx, ny, adx, ady, pass;
    const signed char dx = (tgt_x > cx) ? 1 : (tgt_x < cx) ? -1 : 0;
    const signed char dy = (tgt_y > cy) ? 1 : (tgt_y < cy) ? -1 : 0;

    adx = (unsigned char)(tgt_x > cx ? tgt_x - cx : cx - tgt_x);
    ady = (unsigned char)(tgt_y > cy ? tgt_y - cy : cy - tgt_y);

#define OPEN(X, Y) ((X) < gw && (Y) < gh && \
                    !chase_blocked((X), (Y), through_walls, avoid_bodies))

    /* Twice: first refusing to step back where it came from, then -- only if that
     * left it with nowhere at all -- allowing it.
     *
     * Without the first pass a monster meeting a long wall bounces between two cells
     * forever. Greedy pursuit turns it toward the wall, the wall turns it aside, and
     * the next step turns it straight back; it never gets to the end of the wall.
     * From the player's chair that is indistinguishable from the stalling bug this
     * already fixed once -- something that should be hunting standing about instead --
     * so it gets the same answer: one cell of memory, which is enough to make the
     * sidestep commit to a direction and walk the wall out. */
    for (pass = 0; pass < 2; pass++) {
        nx = cx; ny = cy;

        if (dx && (!dy || adx >= ady)) {
            if (OPEN((unsigned char)(cx + dx), cy)) nx = (unsigned char)(cx + dx);
            else if (dy && OPEN(cx, (unsigned char)(cy + dy)))
                ny = (unsigned char)(cy + dy);
        } else if (dy) {
            if (OPEN(cx, (unsigned char)(cy + dy))) ny = (unsigned char)(cy + dy);
            else if (dx && OPEN((unsigned char)(cx + dx), cy))
                nx = (unsigned char)(cx + dx);
        }

        /* Both ways forward blocked. Sidestep across the direction we wanted, which
         * walks round a body or a wall corner instead of staring at it. */
        if (nx == cx && ny == cy) {
            if (dx) {
                if (OPEN(cx, (unsigned char)(cy - 1)))      ny = (unsigned char)(cy - 1);
                else if (OPEN(cx, (unsigned char)(cy + 1))) ny = (unsigned char)(cy + 1);
            } else if (dy) {
                if (OPEN((unsigned char)(cx - 1), cy))      nx = (unsigned char)(cx - 1);
                else if (OPEN((unsigned char)(cx + 1), cy)) nx = (unsigned char)(cx + 1);
            }
        }

        /* Boxed in on all four sides -- possible once a room fills with bodies. Take
         * any open cell rather than freeze. */
        if (nx == cx && ny == cy) {
            const unsigned char dir = rndn(4);
            const unsigned char tx = (unsigned char)(cx + ((dir == 0) ? 1 : (dir == 1) ? -1 : 0));
            const unsigned char ty = (unsigned char)(cy + ((dir == 2) ? 1 : (dir == 3) ? -1 : 0));
            if (OPEN(tx, ty)) { nx = tx; ny = ty; }
        }

        if (nx != cx || ny != cy) break;    /* moved */
        back_x = 0xFF;                      /* truly stuck: let it double back */
        back_y = 0xFF;
    }
#undef OPEN

    back_x = cx;                            /* where it just came from */
    back_y = cy;
    *px = nx;
    *py = ny;
}

static void monsters_advance(void)
{
    unsigned char i;
    for (i = 0; i < MAX_MON; i++) {
        if (!m_live[i]) continue;
        chase_self = i;
        back_x = m_ox[i];
        back_y = m_oy[i];

        /* Guard a post rather than hunt you across the room.
         *
         * They used to walk straight at you from wherever they were, which made a
         * room a chase: back away and the whole set followed in a line, and the way
         * to clear one was to reverse down a corridor shooting. The arcade's do not
         * do that. They work a patch of floor, dart at you when you come inside it,
         * and drift back when you leave -- so the room is a set of places that are
         * dangerous rather than a pack that is following you, and their bodies end up
         * on the squares that were worth holding. */
        if (taxi(m_x[i], m_y[i], wx, wy) <= MON_AGGRO) {
            tgt_x = wx; tgt_y = wy;                      /* inside its patch: dart */
        } else if (taxi(m_x[i], m_y[i], m_hx[i], m_hy[i]) > MON_ORBIT) {
            tgt_x = m_hx[i]; tgt_y = m_hy[i];            /* strayed: drift back */
        } else {
            tgt_x = m_x[i]; tgt_y = m_y[i];              /* at home: mill about */
        }
        chase(&m_x[i], &m_y[i], 0, 1);      /* walls, bodies and each other stop them */
        m_ox[i] = back_x;
        m_oy[i] = back_y;

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
        back_x = h_ox[i];
        back_y = h_oy[i];
        hall_self = i;
        tgt_x = wx; tgt_y = wy;
        chase(&h_x[i], &h_y[i], phases, 0);
        hall_self = 0xFF;
        h_ox[i] = back_x;
        h_oy[i] = back_y;
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
    unsigned char live = 0;

    if (mode == MODE_ROOM &&                       /* the bow is for rooms */
        tx < gw && ty < gh &&
        grid[ty][tx] == T_FLOOR &&                 /* never over a wall or a body */
        !lethal(tx, ty)) {                         /* let the danger show instead */
        live = 1; f_x = tx; f_y = ty;
    }
    /* Assigned once, at the end. Clearing it first and setting it later leaves a
       window in which the pip does not exist, and anything sampling the game inside
       that window -- a test, or the draw called from the main loop -- sees it flicker
       off. The same shape of bug as erasing a mover before redrawing it. */
    f_live = live;
}

/* Put every mover on its sprite.
 *
 * Sprites draw over the cell plane and are not in it, so nothing here has to be
 * erased first -- a mover that has gone simply has its sprite switched off. That is
 * why the per-tick restore() calls that used to rub each one out are gone from
 * everything except the cells whose GRID changed underneath a mover: a picked-up
 * treasure still has to be repainted, because the tile itself is different now.
 *
 * Slot order is fixed rather than allocated, so a mover keeps the same sprite for
 * its whole life. Overlap is settled by slot number -- lower draws first, so Winky
 * is under a monster standing on him, which is the moment you most want to see. */
static void draw_movers(unsigned char frac)
{
    unsigned char i, op, ow, om, oh;
    unsigned int span, nx, ny;

    /* Winky arrives early, so steering bites. Everything else slides across its
       whole cadence, so it moves continuously instead of lurching and stopping. */
    op = slide(frac, tickrate / SLIDE_DEN);
    ow = slide(frac, tickrate);
    om = slide((unsigned int)(tick_count - mon_at) * tickrate + frac,
               (unsigned int)MON_EVERY * tickrate);
    span = (mode == MODE_MAP) ? (unsigned int)HALL_EVERY * tickrate : tickrate;
    oh = slide((unsigned int)(tick_count - hall_at) * tickrate + frac, span);

    /* Winky first: the pip is placed relative to where he is DRAWN, so it travels
       with him for free and can never detach mid-slide. */
    nx = glide_x(pw_x, wx, op);
    ny = glide_y(pw_y, wy, op);
    /* Half size out in the hall, as the arcade draws him: the hall is a maze of
       corridors and a full-size body fills one, so you cannot see the gap you are
       aiming for. Centred in the cell, since he is smaller than it. Note this is the
       PICTURE only -- he still occupies the whole cell as far as anything he can walk
       into is concerned. */
    if (mode == MODE_MAP) spr_nom(SPR_WINKY, nx + 4, ny + 8, G_WINKY, A_WINKY, 0);
    else                  spr_nom(SPR_WINKY, nx, ny, G_WINKY, A_WINKY, SPR_MAG);

    /* The pip used to be a full-size body a whole cell away. A playfield cell is
       twice as tall as it is wide, so up and down put it 32 pixels off and left and
       right only 16 -- the aim read as lopsided, because it was. It is a reticle
       rather than a body, so it is now half size and placed in PIXELS hard against
       whichever edge of Winky he faces: touching on all four sides, and small enough
       that it reads as an aim rather than as another thing in the room. */
    if (f_live) {
        unsigned int px, py;
        if (face_dx > 0)      { px = nx + 16; py = ny + 8;  }
        else if (face_dx < 0) { px = nx - 8;  py = ny + 8;  }
        else if (face_dy > 0) { px = nx + 4;  py = ny + 32; }
        else                  { px = nx + 4;  py = ny - 16; }
        spr_nom(SPR_FACE, px, py,
                face_dy < 0 ? G_FACE_U : face_dy > 0 ? G_FACE_D :
                face_dx < 0 ? G_FACE_L : G_FACE_R, A_FACE, 0);
    }
    else spr_off(SPR_FACE);

    for (i = 0; i < MAX_MON; i++) {
        if (m_live[i]) spr_nom((unsigned char)(SPR_MON0 + i),
                               glide_x(pm_x[i], m_x[i], om), glide_y(pm_y[i], m_y[i], om),
                               theme_monster[theme],
                               have_treasure ? A_MON_WORTH : A_MON, SPR_MAG);
        else           spr_off((unsigned char)(SPR_MON0 + i));
    }
    for (i = 0; i < MAX_HALL; i++) {
        if (h_live[i]) spr_nom((unsigned char)(SPR_HALL0 + i),
                               glide_x(ph_x[i], h_x[i], oh), glide_y(ph_y[i], h_y[i], oh),
                               G_HALLMON, a_hall, SPR_MAG);
        else           spr_off((unsigned char)(SPR_HALL0 + i));
    }
    if (a_live) spr_nom(SPR_ARROW, glide_x(pa_x, a_x, ow), glide_y(pa_y, a_y, ow),
                        a_dy < 0 ? G_ARROW_U : a_dy > 0 ? G_ARROW_D :
                        a_dx < 0 ? G_ARROW_L : G_ARROW_R, A_ARROW, SPR_MAG);
    else        spr_off(SPR_ARROW);
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

    tick_count++;
    cue_tick();

    /* Snapshot what the next frames will slide FROM, before anything moves. */
    pw_x = wx; pw_y = wy;
    pf_x0 = f_x; pf_y0 = f_y;
    pa_x = a_x; pa_y = a_y;

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
        if (!(tick_count % HALL_EVERY)) {
            for (i = 0; i < MAX_HALL; i++) { ph_x[i] = h_x[i]; ph_y[i] = h_y[i]; }
            hall_at = tick_count;
            hall_advance();
        }
    } else {
        if (grid[wy][wx] == T_TREAS) {
            grid[wy][wx] = T_FLOOR;
            have_treasure = 1;
            treas_got |= (unsigned char)(1 << theme);
            score += (unsigned int)SCORE_TREASURE * level;
            cue(SND_TREASURE);
            draw_hud();
        }
        /* Either doorway is a way out, with or without the goods -- a room you have
         * walked into and thought better of is not a trap. Which one you use decides
         * where in the hall you reappear: leave by the north door and you step out at
         * the north of the room's block. Whether the room counts as LOOTED is a
         * separate question, settled by have_treasure where the room is closed out. */
        if (grid[wy][wx] == T_EXIT) {
            exit_used = (rx_x[1] == wx && rx_y[1] == wy) ? 1 : 0;
            ret_x = sl_x[slot_entered][exit_used];
            ret_y = sl_y[slot_entered][exit_used];
            left_room = 1;
            return;
        }
        if (lethal(wx, wy)) { dead = 1; return; }

        if (ks & KS_FIRE) fire();
        arrow_advance();
        if (!(tick_count % MON_EVERY)) {
            for (i = 0; i < MAX_MON; i++) { pm_x[i] = m_x[i]; pm_y[i] = m_y[i]; }
            mon_at = tick_count;
            monsters_advance();
        }
        if (++dawdle > HALL_ROOM_TICKS) hall_intrude();
        /* Note the sense: every tick EXCEPT every HALL_IN_SKIP'th, so the intruder
         * runs at four fifths of Winky rather than a fraction of him. */
        if (tick_count % HALL_IN_SKIP) {
            for (i = 0; i < MAX_HALL; i++) { ph_x[i] = h_x[i]; ph_y[i] = h_y[i]; }
            hall_at = tick_count;
            hall_advance(); hall_arrow_check();
        }
    }

    if (lethal(wx, wy)) dead = 1;   /* something may have stepped onto Winky */
    if (dead) return;

    draw_facing();
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
    clear_screen();
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
    clear_screen();
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
    clear_screen();
    put_str(22, 9,  "SCORE THIS LEVEL", A_HUD);
    put_num(46, 9,  earned, 6, A_HUD);
    put_str(22, 11, "BONUS MULTIPLIER", A_HUD);
    put_str(46, 11, "X", A_HUD);
    put_num(47, 11, mult, 1, A_HUD);
    put_str(22, 13, "TOTAL BONUS", A_HUD);
    put_num(46, 13, total, 6, A_HUD);
    wait_key();
}

/* The end of a run: what you scored, and whether you want another go. The arcade put
 * a coin slot here; the nearest thing we have is a key. */
static unsigned char game_over_screen(void)
{
    int k;

    vfill(' ');
    clear_screen();
    put_str(31, 8,  "G A M E   O V E R", A_HUD);
    put_str(31, 11, "FINAL SCORE", A_HUD);
    put_num(43, 11, score, 6, A_HUD);
    put_str(31, 14, "PLAY AGAIN?  Y / N", A_TEXT);

    while (INCH_NB() >= 0) { }         /* drain, so a stale byte cannot answer for you */
    for (;;) {
        k = INCH_NB();
        if (k < 0) continue;
        if (swallow_esc(k)) continue;  /* an arrow's ESC [ X, not an answer */
        if (k == 'y' || k == 'Y') return 1;
        if (k == 'n' || k == 'N' || k == 'q' || k == 'Q') return 0;
    }
}

/* Deal this level's four rooms from the six themes, rotating with the level so a
 * run of twelve room-visits is not four layouts seen three times each, and adopt
 * the level's palette. */
static void start_level(void)
{
    const unsigned char p = (unsigned char)((level - 1) % LEVELS);
    unsigned char i;

    hall_of_level = p;
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

/* Everything a run owns, back to the start. The treasure roster goes with it: it is
 * what THIS player has found, and the screen says PLAYER 1 GET READY. */
static void new_game(void)
{
    score = 0;
    lives = LIVES_START;
    level = 1;
    tickrate = TICK_RATE;
    treas_got = 0;
    start_level();
    roster_screen();
}

int main(void)
{
    unsigned int last, now, lastdraw, earned, total;
    unsigned char ks, catchup, i, mult;
    int k;

    rng = rng_seed();
    if (!rng) rng = 0xACE1;

    vhidecur();
    load_font();                /* our glyphs, before anything is drawn with them */
    banner("V E N T U R E");

    new_game();
    enter_map();

    for (;;) {
        dead = 0;
        left_room = 0;
        last = jiffies();
        lastdraw = last - 1;          /* force a first draw */

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

            /* Redraw every jiffy rather than every tick, sliding each mover the
               fraction of its step that has elapsed. This is the whole point of
               putting the movers on sprites: the simulation still runs at tickrate
               and still thinks in whole cells, but the picture no longer has to. */
            if (now != lastdraw) {
                draw_movers((unsigned char)(now - last));
                lastdraw = now;
            }

            /* The FIFO carries the commands that are not movement. Drained a few
             * per pass so a burst cannot back up and start dropping bytes. */
            for (catchup = 0; catchup < 4; catchup++) {
                k = INCH_NB();
                if (k < 0) break;
                if (swallow_esc(k)) continue;   /* an arrow's ESC [ X, not a command */
                if (k == 'q' || k == 'Q') { sound_off(); restore_font(); QUITDOS(); return 0; }
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
            /* Only a looted room is done. Walking back out empty-handed leaves it
               open in the hall, to be gone back into. */
            if (have_treasure) slot_done[slot_entered] = 1;

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
                sound_off();
                snd_left = 0;
                if (!game_over_screen()) { restore_font(); QUITDOS(); return 0; }
                new_game();
                enter_map();
                continue;
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
