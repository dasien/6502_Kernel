/* ============================================================================
 * raster.c -- RASTER: splits made partway down the screen.
 *
 * The VIC reports which line the beam is on, and a palette, fine-scroll or font
 * change made on a visible line shows from that line down. Two classic effects
 * on one screen of text:
 *
 *   - COLOUR BARS. Between lines 32 and 199 the background colour -- palette
 *     slot 0 -- changes every six lines, walking through a gradient that slides
 *     a little each frame. The text sits on top; the bars show behind it.
 *   - WOBBLE. From line 200 down, the fine-scroll offset changes every eight
 *     lines, following a sine wave, so the bottom half ripples.
 *
 * Two ways to make the splits, and SPACE switches between them:
 *
 *   - POLLING. Each frame, wait for it to start, then wait for each line in turn
 *     and make that line's change. The CPU spends the frame waiting.
 *   - INTERRUPT. The same changes are a copper list -- a table of (line, register,
 *     value) -- and the VIC's raster interrupt makes each on its line. The main
 *     loop only rewrites the colours in the blanking lines, ready for the next
 *     frame, and is otherwise free.
 *
 * The picture is the same either way, which is the point. Nothing is redrawn;
 * only two settings move. Q returns to the DOS.
 * ==========================================================================*/

extern int           INCH_NB(void);
extern void          QUITDOS(void);
extern void          vaddr(unsigned int cell);
extern void          vputc(unsigned char ch);
extern void          vattr(unsigned char a);
extern void          vfill(unsigned char ch);
extern void          vcmd(unsigned char cmd);
extern void          vhidecur(void);
extern void          vscrolltop(unsigned char row);
extern void          vpseek(unsigned char index);
extern void          vpwrite(unsigned char b);
extern void          wait_frame(void);
extern void          wait_line(unsigned int line);
extern void          copper_start(const unsigned char *list);
extern void          copper_stop(void);

#define VCMD_CLEAR         0x01
#define VCMD_FINEY         0x0C
#define VCMD_PALETTE_RESET 0x0D

#define BARS_TOP     32         /* below the two title rows */
#define BARS_STEP    6
#define SPLIT        200        /* where the bars end and the wobble begins */
#define WOBBLE_STEP  8
#define VISIBLE      400

/* VIC registers, as the low byte of their $FExx address -- the copper list's form. */
#define R_PAL_IDX    0xCB
#define R_PAL_DATA   0xCC
#define R_CMD        0x32
#define R_CMD_PARAM  0x36

/* The bar gradient, 32 colours: a warm copper bar, then a cool one, each fading in
 * and out through black. And the wobble, a sine of fine-scroll offsets 0-8. */
/* Three arrays rather than one of triples: indexing [k][3] made cc65 call a multiply
 * routine for every bar, which is most of why the rewrite once overran the frame. */
static const unsigned char bar_r[32] = {
    0, 49, 97, 141, 180, 212, 235, 250, 255, 250, 235, 212, 180, 141, 97, 49,
    0, 1, 5, 12, 19, 27, 34, 38, 40, 38, 34, 27, 20, 12, 5, 1
};
static const unsigned char bar_g[32] = {
    0, 6, 24, 52, 84, 117, 145, 163, 170, 163, 145, 117, 85, 52, 24, 6,
    0, 23, 45, 66, 84, 99, 110, 117, 120, 117, 110, 99, 84, 66, 45, 23
};
static const unsigned char bar_b[32] = {
    0, 1, 5, 12, 19, 27, 34, 38, 40, 38, 34, 27, 20, 12, 5, 1,
    0, 49, 97, 141, 180, 212, 235, 250, 255, 250, 235, 212, 180, 141, 97, 49
};
static const unsigned char wobble[32] = { 4, 5, 6, 6, 7, 7, 8, 8, 8, 8, 8, 7, 7, 6, 6, 5, 4, 3, 2, 2, 1, 1, 0, 0, 0, 0, 0, 1, 1, 2, 2, 3 };

static void say(unsigned int cell, unsigned char attr, const char *p)
{
    vattr(attr);
    vaddr(cell);
    while (*p) vputc((unsigned char)*p++);
}

static void title(void)
{
    say(0, 0x4F, "RASTER SPLITS: the VIC tells a program which line the beam is on");
    say(80, 0x46, "colour bars above line 200, a fine-scroll wobble below.  SPACE: mode  Q: quit");
}

static void show_mode(unsigned char irq)
{
    say(66, 0x4F, irq ? " INTERRUPT  " : " POLLING    ");
}

/* ---- the copper list ------------------------------------------------------
 * Line 0 sets black and no offset; each bar is a palette write of four entries
 * (index, then red, green, blue); line 200 is black again; each wobble step is a
 * fine-scroll command of two (parameter, then the command). The lines and
 * registers are laid down once; each frame only the colours and offsets change,
 * which is what `bar_at` and `wob_at` point at. */
#define N_BARS     ((SPLIT - BARS_TOP) / BARS_STEP)
#define N_WOB      ((VISIBLE - SPLIT) / WOBBLE_STEP)
#define N_ENTRIES  (6 + 4 * N_BARS + 4 + 2 * N_WOB)
static unsigned char list[N_ENTRIES * 4 + 2];
static unsigned char *bar_at[N_BARS];       /* each bar's red; green and blue follow */
static unsigned char *wob_at[N_WOB];        /* each wobble step's offset */
/* bar_at[] and wob_at[] are evenly spaced -- 16 and 8 bytes -- which animate_list
 * relies on to walk them with one pointer. */
static unsigned char *put;

static unsigned char *entry(unsigned int line, unsigned char reg, unsigned char value)
{
    unsigned char *e = put;
    put[0] = (unsigned char)line;
    put[1] = (unsigned char)(line >> 8);
    put[2] = reg;
    put[3] = value;
    put += 4;
    return e + 3;                           /* where the value lives */
}

static void colour(unsigned int line, unsigned char r, unsigned char g, unsigned char b)
{
    entry(line, R_PAL_IDX, 0);
    entry(line, R_PAL_DATA, r);
    entry(line, R_PAL_DATA, g);
    entry(line, R_PAL_DATA, b);
}

static void build_list(void)
{
    unsigned char i;
    unsigned int line;
    put = list;
    colour(0, 0, 0, 0);
    entry(0, R_CMD_PARAM, 0);
    entry(0, R_CMD, VCMD_FINEY);
    for (i = 0, line = BARS_TOP; i < N_BARS; ++i, line += BARS_STEP) {
        bar_at[i] = entry(line, R_PAL_IDX, 0) + 4;
        entry(line, R_PAL_DATA, 0);
        entry(line, R_PAL_DATA, 0);
        entry(line, R_PAL_DATA, 0);
    }
    colour(SPLIT, 0, 0, 0);
    for (i = 0, line = SPLIT; i < N_WOB; ++i, line += WOBBLE_STEP) {
        wob_at[i] = entry(line, R_CMD_PARAM, 0);
        entry(line, R_CMD, VCMD_FINEY);
    }
    put[0] = 0;
    put[1] = 0xFF;                          /* the end */
}

/* The next frame's colours and offsets, written into the list in place, in two halves
 * so each has time to finish. The bars can be rewritten once the beam is past them,
 * below line 200; the wobble once it is past line 400, in the blanking lines. A bar is
 * 16 bytes of list with its red 4 bytes in, a wobble step 8 bytes.
 *
 * Statics, not locals: cc65 keeps locals on a software stack and reaches each through a
 * helper call, and the first version of this -- locals, and a [k][3] table -- ran past
 * the frame boundary, so the animation dropped to every other frame. */
static unsigned char *ap;
static unsigned char ai, ak;

static void animate_bars(unsigned char phase)
{
    ap = bar_at[0];
    for (ai = 0, ak = phase; ai < N_BARS; ++ai, ++ak, ap += 16) {
        ak &= 31;
        ap[0] = bar_r[ak];
        ap[4] = bar_g[ak];
        ap[8] = bar_b[ak];
    }
}

static void animate_wobble(unsigned char phase)
{
    ap = wob_at[0];
    for (ai = 0, ak = (unsigned char)(phase << 1); ai < N_WOB; ++ai, ++ak, ap += 8)
        *ap = wobble[ak & 31];
}

static void text(void)
{
    static const char line[] =
        "The quick brown fox jumps over the lazy dog. 0123456789 !@#$%^&*() ";
    unsigned char row, col, i;
    for (row = 2; row < 25; ++row) {
        vattr((unsigned char)(0x40 | (row % 6 + 1)));   /* bright fg, bg slot 0 */
        vaddr((unsigned int)row * 80);
        i = (unsigned char)(row * 5 % (sizeof line - 1));
        for (col = 0; col < 80; ++col) {
            vputc((unsigned char)line[i]);
            if (++i == sizeof line - 1) i = 0;
        }
    }
}

static void background(unsigned char r, unsigned char g, unsigned char b)
{
    vpseek(0);                  /* slot 0, the background every cell here uses */
    vpwrite(r); vpwrite(g); vpwrite(b);
}

static void fine(unsigned char px)
{
    vfill(px);
    vcmd(VCMD_FINEY);
}

/* One frame of the polling version: wait for each line, make its change. */
static void polled_frame(unsigned char phase)
{
    unsigned char k;
    unsigned int line;

    /* Line 0: the top of the frame. Black background, no offset. */
    wait_frame();
    background(0, 0, 0);
    fine(0);

    /* The bars: a new background colour every six lines. */
    k = phase;
    for (line = BARS_TOP; line < SPLIT; line += BARS_STEP) {
        wait_line(line);
        background(bar_r[k & 31], bar_g[k & 31], bar_b[k & 31]);
        ++k;
    }

    /* Line 200: back to black, and the wobble -- a new offset every eight. */
    wait_line(SPLIT);
    background(0, 0, 0);
    k = (unsigned char)(phase << 1);
    for (line = SPLIT; line < VISIBLE; line += WOBBLE_STEP) {
        wait_line(line);
        fine(wobble[k & 31]);
        ++k;
    }
}

void main(void)
{
    unsigned char phase = 0, irq = 0;
    int key;

    vattr(0x07);
    vfill(' ');
    vcmd(VCMD_CLEAR);
    vhidecur();
    title();
    text();
    vscrolltop(2);              /* the title rows stay out of the fine scroll */
    build_list();
    show_mode(irq);

    while (INCH_NB() >= 0) ;

    for (;;) {
        if (irq) {
            /* The interrupt makes the splits. Rewrite each half of the list once the
               beam is past it, then wait out the frame so it happens once. */
            wait_line(SPLIT);
            animate_bars((unsigned char)(phase + 1));
            wait_line(VISIBLE);
            animate_wobble((unsigned char)(phase + 1));
            wait_frame();
        } else {
            polled_frame(phase);
        }
        ++phase;

        key = INCH_NB();
        if (key == 'q' || key == 'Q') break;
        if (key == ' ') {
            irq ^= 1;
            if (irq) { animate_bars(phase); animate_wobble(phase); copper_start(list); }
            else     copper_stop();
            show_mode(irq);
        }
    }

    copper_stop();              /* the handler lives in this program's memory */
    vcmd(VCMD_PALETTE_RESET);
    vattr(0x02);
    vfill(' ');
    vcmd(VCMD_CLEAR);           /* also ends fine scrolling and the scroll region */
    QUITDOS();
}
