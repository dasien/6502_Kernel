/* ============================================================================
 * sprdemo.c -- SPRDEMO: bitmap sprites from the VIC's pattern RAM.
 *
 * Sprites move over a screen of text, which shows through wherever a pattern
 * pixel is 0. Every animation here is the same trick: the frames are uploaded
 * once, into consecutive slots, and a single write to the glyph register switches
 * a sprite between them. Nothing is redrawn to animate.
 *
 *   - smileys that blink and hearts that beat: two frames each;
 *   - coins that spin as they bounce: full face, oval, edge, oval;
 *   - figures that walk: four frames of 16x32, each frame two slots stacked, so
 *     a frame is picked by its TOP slot and the chip draws the one after it below;
 *   - a still block in the corner, four slots composed 2x2, to show sizes
 *     counting slots.
 *
 * The art is written as text below, one character a pixel: '.' for transparent
 * and a hex digit for a palette slot. It is packed into the chip's 4-bit format
 * at start-up. Any key returns to the DOS.
 * ==========================================================================*/

/* ---- runtime glue (libmfcglue) ---- */
extern int           INCH_NB(void);
extern void          QUITDOS(void);
extern void          vaddr(unsigned int cell);
extern void          vputc(unsigned char ch);
extern void          vattr(unsigned char a);
extern void          vfill(unsigned char ch);
extern void          vcmd(unsigned char cmd);
extern void          vhidecur(void);
extern void          wait_frame(void);
extern void          present(void);
extern void          spr_sel(unsigned char index);
extern void          spr_x_px(unsigned int px);
extern void          spr_y_px(unsigned int py);
extern void          spr_glyph(unsigned char glyph);
extern void          spr_on(unsigned char enable);
extern void          spr_w(unsigned char cells);
extern void          spr_h(unsigned char cells);
extern void          spr_mag(unsigned char axes);
extern void          spr_img_seek(unsigned char slot);
extern void          spr_img_load(const unsigned char *src);
extern void          spr_bitmap(unsigned char on);

#define VCMD_CLEAR   0x01

/* Pattern slots. The block of four must be consecutive: a 2x2 sprite draws its
 * base slot and the three after it, row-major. */
#define SLOT_SMILE   0
#define SLOT_BLINK   1
#define SLOT_HEART   2
#define SLOT_BEAT    3
#define SLOT_BLOCK   4          /* 4..7 */
#define SLOT_WALK    8          /* 8..15: four frames, top slot then legs */
#define SLOT_COIN    16         /* 16..18: full, oval, edge */

#define BOUNCERS     8          /* sprites 0..7: smileys and hearts */
#define COINS        4          /* 8..11 */
#define MOVERS       (BOUNCERS + COINS)
#define WALKERS      4          /* 12..15 */
#define BLOCK_SPR    16
#define SIZE_PX      32         /* a 16x16 slot, magnified on both axes */
#define MAX_X        (640 - SIZE_PX)
#define MAX_Y        (400 - SIZE_PX)

/* The default palette is ANSI order: 1 red, 3 brown, 4 blue, 9 bright red,
 * B bright yellow, F white. */
static const char *const smile_art[16] = {
    ".....333333.....",
    "...33BBBBBB33...",
    "..3BBBBBBBBBB3..",
    ".3BBBBBBBBBBBB3.",
    ".3BBB44BB44BBB3.",
    "3BBBB44BB44BBBB3",
    "3BBBB44BB44BBBB3",
    "3BBBBBBBBBBBBBB3",
    "3BBBBBBBBBBBBBB3",
    "3BB1BBBBBBBB1BB3",
    "3BBB1BBBBBB1BBB3",
    ".3BBB111111BBB3.",
    ".3BBBBBBBBBBBB3.",
    "..3BBBBBBBBBB3..",
    "...33BBBBBB33...",
    ".....333333.....",
};

static const char *const blink_art[16] = {
    ".....333333.....",
    "...33BBBBBB33...",
    "..3BBBBBBBBBB3..",
    ".3BBBBBBBBBBBB3.",
    ".3BBBBBBBBBBBB3.",
    "3BBB444BB444BBB3",
    "3BBBBBBBBBBBBBB3",
    "3BBBBBBBBBBBBBB3",
    "3BBBBBBBBBBBBBB3",
    "3BB1BBBBBBBB1BB3",
    "3BBB1BBBBBB1BBB3",
    ".3BBB111111BBB3.",
    ".3BBBBBBBBBBBB3.",
    "..3BBBBBBBBBB3..",
    "...33BBBBBB33...",
    ".....333333.....",
};

static const char *const heart_art[16] = {
    "................",
    "..999......999..",
    ".99999....99999.",
    "99FF999..9999999",
    "99F9999999999999",
    "9999999999999999",
    "9999999999999999",
    ".99999999999999.",
    ".99999999999999.",
    "..999999999999..",
    "...9999999999...",
    "....99999999....",
    ".....999999.....",
    "......9999......",
    ".......99.......",
    "................",
};

static const char *const beat_art[16] = {
    "................",
    "................",
    "...111....111...",
    "..11991..119111.",
    "..19111111111111",
    "..11111111111111",
    "...111111111111.",
    "...111111111111.",
    "....1111111111..",
    ".....11111111...",
    "......111111....",
    ".......1111.....",
    "........11......",
    "................",
    "................",
    "................",
};


/* The walker, facing right. The near leg is blue (4) and the far one grey (8), so
 * the two strides are visibly different frames rather than one frame twice. Arms
 * swing out on a stride and hang at the sides on a pass. */
static const char *const walk_swing[16] = {
    "......3333......",
    ".....333333.....",
    ".....3FFFF3.....",
    ".....FFF4FF.....",
    ".....FFFFFF.....",
    "......FFFF......",
    ".......FF.......",
    ".....CCCCCC.....",
    "....CCCCCCCC....",
    "...CC.CCCC.CC...",
    "..CC..CCCC..CC..",
    ".FF...CCCC...FF.",
    "......CCCC......",
    "......CCCC......",
    "......CCCC......",
    "......4444......",
};

static const char *const walk_sides[16] = {
    "......3333......",
    ".....333333.....",
    ".....3FFFF3.....",
    ".....FFF4FF.....",
    ".....FFFFFF.....",
    "......FFFF......",
    ".......FF.......",
    ".....CCCCCC.....",
    "....CCCCCCCC....",
    "....C.CCCC.C....",
    "....C.CCCC.C....",
    "....C.CCCC.C....",
    "....F.CCCC.F....",
    "......CCCC......",
    "......CCCC......",
    "......4444......",
};

static const char *const legs_stride_near[16] = {
    "......4444......",
    "......8844......",
    ".....88..44.....",
    ".....88..44.....",
    "....88....44....",
    "....88....44....",
    "...88......44...",
    "...88......44...",
    "..88........44..",
    "..88........44..",
    ".888........444.",
    ".888........4444",
    "................",
    "................",
    "................",
    "................",
};

static const char *const legs_pass_near[16] = {
    "......4444......",
    "......8844......",
    "......8844......",
    "......88.44.....",
    "......88..44....",
    "......88..44....",
    "......88.44.....",
    "......88.44.....",
    "......88.44.....",
    "......88.444....",
    "......888.......",
    "......8888......",
    "................",
    "................",
    "................",
    "................",
};

static const char *const legs_stride_far[16] = {
    "......4444......",
    "......4488......",
    ".....44..88.....",
    ".....44..88.....",
    "....44....88....",
    "....44....88....",
    "...44......88...",
    "...44......88...",
    "..44........88..",
    "..44........88..",
    ".444........888.",
    ".444........8888",
    "................",
    "................",
    "................",
    "................",
};

static const char *const legs_pass_far[16] = {
    "......4444......",
    "......4488......",
    "......4488......",
    "......44.88.....",
    "......44..88....",
    "......44..88....",
    "......44.88.....",
    "......44.88.....",
    "......44.88.....",
    "......44.888....",
    "......444.......",
    "......4444......",
    "................",
    "................",
    "................",
    "................",
};

static const char *const coin_full[16] = {
    ".....333333.....",
    "...33BBBBBB33...",
    "..3BBFFBBBBBB3..",
    ".3BBFBBBBBBBBB3.",
    ".3BFBBBBBBBBBB3.",
    "3BBFBBBBBBBBBBB3",
    "3BBBBBBBBBBBBBB3",
    "3BBBBBBBBBBBBBB3",
    "3BBBBBBBBBBBBBB3",
    "3BBBBBBBBBBBBBB3",
    "3BBBBBBBBBBBBBB3",
    ".3BBBBBBBBBBBB3.",
    ".3BBBBBBBBBBBB3.",
    "..3BBBBBBBBBB3..",
    "...33BBBBBB33...",
    ".....333333.....",
};

static const char *const coin_oval[16] = {
    "......3333......",
    ".....3BBBB3.....",
    "....3BFBBBB3....",
    "....3FBBBBB3....",
    "....3FBBBBB3....",
    "....3FBBBBB3....",
    "....3BBBBBB3....",
    "....3BBBBBB3....",
    "....3BBBBBB3....",
    "....3BBBBBB3....",
    "....3BBBBBB3....",
    "....3BBBBBB3....",
    "....3BBBBBB3....",
    "....3BBBBBB3....",
    ".....3BBBB3.....",
    "......3333......",
};

static const char *const coin_edge[16] = {
    ".......33.......",
    "......3BB3......",
    "......3FB3......",
    "......3FB3......",
    "......3BB3......",
    "......3BB3......",
    "......3BB3......",
    "......3BB3......",
    "......3BB3......",
    "......3BB3......",
    "......3BB3......",
    "......3BB3......",
    "......3BB3......",
    "......3BB3......",
    "......3BB3......",
    ".......33.......",
};

/* Spin order, as slot offsets from SLOT_COIN: the oval is both quarter turns. */
static const unsigned char coin_turn[4] = { 0, 1, 2, 1 };

static unsigned char packed[128];

static unsigned char nibble(char c)
{
    if (c >= '0' && c <= '9') return (unsigned char)(c - '0');
    if (c >= 'A' && c <= 'F') return (unsigned char)(c - 'A' + 10);
    return 0;                   /* '.' and anything else: transparent */
}

/* Text art -> one slot: two pixels a byte, the left one in the high nibble. */
static void load_art(unsigned char slot, const char *const *art)
{
    unsigned char y, x, n = 0;
    for (y = 0; y < 16; ++y)
        for (x = 0; x < 16; x += 2)
            packed[n++] = (unsigned char)((nibble(art[y][x]) << 4) | nibble(art[y][x + 1]));
    spr_img_seek(slot);
    spr_img_load(packed);
}

/* The corner block: 32x32 pixels across four slots, a diagonal band through all
 * fifteen colours so every slot boundary is visible if the order is wrong. */
static void load_block(void)
{
    unsigned char s, y, x, n, px, py;
    for (s = 0; s < 4; ++s)
    {
        n = 0;
        for (y = 0; y < 16; ++y)
        {
            py = (unsigned char)(y + ((s & 2) ? 16 : 0));
            for (x = 0; x < 16; x += 2)
            {
                px = (unsigned char)(x + ((s & 1) ? 16 : 0));
                packed[n++] = (unsigned char)((((px + py) / 4 % 15 + 1) << 4) |
                                              ((px + 1 + py) / 4 % 15 + 1));
            }
        }
        spr_img_seek((unsigned char)(SLOT_BLOCK + s));
        spr_img_load(packed);
    }
}

static int           mx[MOVERS], my[MOVERS];
static signed char   mdx[MOVERS], mdy[MOVERS];
static unsigned int  wx[WALKERS];

/* Four walk frames of two slots each: stride, pass, the other stride, the other pass. */
static void load_walker(void)
{
    load_art(SLOT_WALK + 0, walk_swing);
    load_art(SLOT_WALK + 1, legs_stride_near);
    load_art(SLOT_WALK + 2, walk_sides);
    load_art(SLOT_WALK + 3, legs_pass_near);
    load_art(SLOT_WALK + 4, walk_swing);
    load_art(SLOT_WALK + 5, legs_stride_far);
    load_art(SLOT_WALK + 6, walk_sides);
    load_art(SLOT_WALK + 7, legs_pass_far);
}

static void backdrop(void)
{
    static const char line[] =
        "SPRITES FROM PATTERN RAM  *  0 IS TRANSPARENT  *  ANY KEY QUITS  *  ";
    unsigned char row, col, i;
    vattr(0x07);
    vfill(' ');
    vcmd(VCMD_CLEAR);
    vhidecur();
    for (row = 0; row < 25; ++row)
    {
        vattr((unsigned char)(row & 1 ? 0x08 : 0x02));
        vaddr((unsigned int)row * 80);
        i = (unsigned char)(row * 7 % (sizeof line - 1));
        for (col = 0; col < 80; ++col)
        {
            vputc((unsigned char)line[i]);
            if (++i == sizeof line - 1) i = 0;
        }
    }
}

void main(void)
{
    unsigned char i, frame = 0, beat;

    backdrop();
    load_art(SLOT_SMILE, smile_art);
    load_art(SLOT_BLINK, blink_art);
    load_art(SLOT_HEART, heart_art);
    load_art(SLOT_BEAT, beat_art);
    load_block();
    load_walker();
    load_art(SLOT_COIN + 0, coin_full);
    load_art(SLOT_COIN + 1, coin_oval);
    load_art(SLOT_COIN + 2, coin_edge);

    for (i = 0; i < MOVERS; ++i)
    {
        mx[i] = 20 + i * 37;
        my[i] = 16 + (int)((unsigned int)i * 53 % (MAX_Y - 16));
        mdx[i] = (signed char)((i & 1) ? -(1 + i % 3) : (1 + i % 3));
        mdy[i] = (signed char)((i & 2) ? -(1 + (i + 1) % 2) : (1 + (i + 1) % 2));
        spr_sel(i);
        spr_glyph(i >= BOUNCERS ? SLOT_COIN : (i & 1) ? SLOT_HEART : SLOT_SMILE);
        spr_mag(3);
        spr_bitmap(1);
        spr_on(1);
    }

    /* Walkers: one slot wide, two tall, magnified to 32x64, each on its own lane. */
    for (i = 0; i < WALKERS; ++i)
    {
        wx[i] = (unsigned int)i * 160;
        spr_sel((unsigned char)(MOVERS + i));
        spr_y_px(24 + (unsigned int)i * 92);
        spr_w(1);
        spr_h(2);
        spr_mag(3);
        spr_bitmap(1);
        spr_on(1);
    }

    spr_sel(BLOCK_SPR);
    spr_x_px(640 - 40);
    spr_y_px(400 - 40);
    spr_glyph(SLOT_BLOCK);
    spr_w(2);
    spr_h(2);
    spr_bitmap(1);
    spr_on(1);

    while (INCH_NB() >= 0) ;    /* nothing typed before we started counts */

    for (;;)
    {
        wait_frame();
        ++frame;
        beat = (unsigned char)((frame & 0x10) ? SLOT_BEAT : SLOT_HEART);

        for (i = 0; i < MOVERS; ++i)
        {
            /* Clamp at the edge and turn round, so a position never goes
               negative -- the register would read it as the far side. */
            mx[i] += mdx[i];
            my[i] += mdy[i];
            if (mx[i] < 0)          { mx[i] = 0;     mdx[i] = (signed char)-mdx[i]; }
            else if (mx[i] > MAX_X) { mx[i] = MAX_X; mdx[i] = (signed char)-mdx[i]; }
            if (my[i] < 0)          { my[i] = 0;     mdy[i] = (signed char)-mdy[i]; }
            else if (my[i] > MAX_Y) { my[i] = MAX_Y; mdy[i] = (signed char)-mdy[i]; }

            spr_sel(i);
            spr_x_px((unsigned int)mx[i]);
            spr_y_px((unsigned int)my[i]);
            if (i >= BOUNCERS)  /* a quarter turn every 6 frames, each coin its own phase */
                spr_glyph((unsigned char)(SLOT_COIN +
                                          coin_turn[((frame / 6) + i) & 3]));
            else if (i & 1)
                spr_glyph(beat);
            else                /* each smiley blinks for 8 frames in 128, staggered */
                spr_glyph((unsigned char)(((frame + i * 16) & 0x7F) < 8
                                          ? SLOT_BLINK : SLOT_SMILE));
        }

        /* A pixel a frame and a new pose every 8, stepping on at the right edge
           back to the left: x cannot go negative, but it can run off the right,
           since the register holds ten bits and the screen needs fewer. */
        for (i = 0; i < WALKERS; ++i)
        {
            if (++wx[i] > 640) wx[i] = 0;
            spr_sel((unsigned char)(MOVERS + i));
            spr_x_px(wx[i]);
            spr_glyph((unsigned char)(SLOT_WALK +
                                      ((((frame >> 3) + i) & 3) << 1)));
        }
        present();

        if (INCH_NB() >= 0) break;
    }

    vattr(0x02);
    vfill(' ');
    vcmd(VCMD_CLEAR);           /* also turns every sprite off */
    QUITDOS();
}
