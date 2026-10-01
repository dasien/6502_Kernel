/* ============================================================================
 * chimes.c -- CHIMES: candidates for the boot chime, played the kernel's way.
 *
 * Each chime is a list of timed SID writes: (wait, register, value) triples,
 * where `wait` is the jiffies since the previous write and $FF ends the list.
 * That is the whole format, and it is the one the kernel will play from its
 * jiffy interrupt, so the chime picked here goes into the ROM byte for byte --
 * nothing is re-arranged between hearing it and shipping it. A filter sweep is
 * just more writes; there is no second mechanism for it.
 *
 * Every list starts by quieting all three voices and ends by putting the chip
 * back the way the kernel expects it (no filter, volume 15), because the reset
 * that plays it does not reset the SID.
 *
 * 1-5 play a chime   Q quit
 * ==========================================================================*/

extern int           INCH_NB(void);
extern void          QUITDOS(void);
extern void          vaddr(unsigned int cell);
extern void          vputc(unsigned char ch);
extern void          vattr(unsigned char a);
extern void          vfill(unsigned char ch);
extern void          vcmd(unsigned char cmd);
extern void          vhidecur(void);
extern void          wait_frame(void);

#define VCMD_CLEAR   0x01
#define SID          ((volatile unsigned char *)0xFE38)
#define SOUND_ENABLE (*(volatile unsigned char *)0x29)     /* the kernel's mute */

/* Register numbers, from $FE38. Voice n's seven start at 7n. */
#define V1  0
#define V2  7
#define V3  14
#define FREQ_LO  0
#define FREQ_HI  1
#define PW_HI    3
#define CTRL     4
#define AD       5
#define SR       6
#define FC_LO    0x15
#define FC_HI    0x16
#define RES_FILT 0x17
#define MODE_VOL 0x18

#define END      0xFF

/* SID frequency values (Hz * 2^24 / 1 MHz), the numbers a C64 tune would use. */
#define C3   2195u
#define G3   3288u
#define C4   4389u
#define E4   5530u
#define G4   6577u
#define C5   8779u
#define E5  11060u
#define G5  13935u
#define C6  17557u

#define W(t, r, v)      (t), (r), (v)
#define NOTE(t, v, f)   W(t, (v) + FREQ_LO, (f) & 0xFF), W(0, (v) + FREQ_HI, (f) >> 8)
#define QUIET           W(0, V1 + CTRL, 0), W(0, V2 + CTRL, 0), W(0, V3 + CTRL, 0), \
                        W(0, RES_FILT, 0), W(0, MODE_VOL, 0x0F)
#define TIDY(t)         W(t, V1 + CTRL, 0), W(0, V2 + CTRL, 0), W(0, V3 + CTRL, 0), \
                        W(0, RES_FILT, 0), W(0, MODE_VOL, 0x0F), END

/* 1. A struck chord. C-G-E spread over an octave and a half, three pulse waves
 *    struck together and left to ring out -- the Macintosh's idea. No timing to
 *    speak of: gate on, and the envelope does the rest. */
static const unsigned char chime_chord[] = {
    QUIET,
    W(0, V1 + PW_HI, 0x08), W(0, V2 + PW_HI, 0x08), W(0, V3 + PW_HI, 0x08),
    W(0, V1 + AD, 0x0B), W(0, V1 + SR, 0x0A),
    W(0, V2 + AD, 0x0B), W(0, V2 + SR, 0x0A),
    W(0, V3 + AD, 0x0B), W(0, V3 + SR, 0x0A),
    NOTE(0, V1, C4), NOTE(0, V2, G4), NOTE(0, V3, E5),
    W(0, V1 + CTRL, 0x41), W(0, V2 + CTRL, 0x41), W(0, V3 + CTRL, 0x41),
    W(90, V1 + CTRL, 0x40), W(0, V2 + CTRL, 0x40), W(0, V3 + CTRL, 0x40),
    TIDY(90)
};

/* 2. A rising arpeggio. C-E-G a twelfth of a second apart, each left ringing on
 *    its own voice, then the first voice re-struck an octave up and held. */
static const unsigned char chime_arpeggio[] = {
    QUIET,
    W(0, V1 + PW_HI, 0x04), W(0, V2 + PW_HI, 0x04), W(0, V3 + PW_HI, 0x04),
    W(0, V1 + AD, 0x09), W(0, V1 + SR, 0x49),
    W(0, V2 + AD, 0x09), W(0, V2 + SR, 0x49),
    W(0, V3 + AD, 0x09), W(0, V3 + SR, 0x49),
    NOTE(0, V1, C5), W(0, V1 + CTRL, 0x41),
    NOTE(5, V2, E5), W(0, V2 + CTRL, 0x41),
    NOTE(5, V3, G5), W(0, V3 + CTRL, 0x41),
    W(5, V1 + CTRL, 0x40),                          /* drop the gate to re-strike */
    NOTE(1, V1, C6), W(0, V1 + CTRL, 0x41),
    W(40, V1 + CTRL, 0x40), W(0, V2 + CTRL, 0x40), W(0, V3 + CTRL, 0x40),
    TIDY(50)
};

/* 3. A bell, "ding-dong". Triangle waves, a sharp strike and a long ring: E over
 *    an octave below it, then C over its octave, the falling third of a doorbell. */
static const unsigned char chime_bell[] = {
    QUIET,
    W(0, V1 + AD, 0x0A), W(0, V1 + SR, 0x0A),
    W(0, V2 + AD, 0x0A), W(0, V2 + SR, 0x0A),
    W(0, V3 + AD, 0x0A), W(0, V3 + SR, 0x0A),
    NOTE(0, V1, E5), NOTE(0, V3, E4),
    W(0, V1 + CTRL, 0x11), W(0, V3 + CTRL, 0x11),
    W(24, V3 + CTRL, 0x10),                         /* drop the gate to re-strike */
    NOTE(1, V2, C5), NOTE(0, V3, C4),
    W(0, V2 + CTRL, 0x11), W(0, V3 + CTRL, 0x11),
    W(50, V1 + CTRL, 0x10), W(0, V2 + CTRL, 0x10), W(0, V3 + CTRL, 0x10),
    TIDY(90)
};

/* 4. A filter swell. A sawtooth chord fades in under a resonant low-pass whose
 *    cutoff climbs for a second, so the sound brightens as it arrives: powering up. */
static const unsigned char chime_swell[] = {
    QUIET,
    W(0, V1 + AD, 0xA0), W(0, V1 + SR, 0xF9),
    W(0, V2 + AD, 0xA0), W(0, V2 + SR, 0xF9),
    W(0, V3 + AD, 0xA0), W(0, V3 + SR, 0xF9),
    NOTE(0, V1, C3), NOTE(0, V2, G3), NOTE(0, V3, E4),
    W(0, FC_LO, 0), W(0, FC_HI, 0x04),
    W(0, RES_FILT, 0xF7), W(0, MODE_VOL, 0x1F),     /* all three through LP, res 15 */
    W(0, V1 + CTRL, 0x21), W(0, V2 + CTRL, 0x21), W(0, V3 + CTRL, 0x21),
    W(3, FC_HI, 0x10), W(3, FC_HI, 0x1C), W(3, FC_HI, 0x28), W(3, FC_HI, 0x34),
    W(3, FC_HI, 0x40), W(3, FC_HI, 0x4C), W(3, FC_HI, 0x58), W(3, FC_HI, 0x64),
    W(3, FC_HI, 0x70), W(3, FC_HI, 0x7C), W(3, FC_HI, 0x88), W(3, FC_HI, 0x94),
    W(3, FC_HI, 0xA0), W(3, FC_HI, 0xAC), W(3, FC_HI, 0xB8), W(3, FC_HI, 0xC4),
    W(20, V1 + CTRL, 0x20), W(0, V2 + CTRL, 0x20), W(0, V3 + CTRL, 0x20),
    TIDY(50)
};

/* 5. The bell reversed, in the chord's voice. C over its octave, then E over its
 *    octave -- the doorbell's third rising instead of falling -- on pulse waves
 *    in the struck chord's voice, every time in it half the chord's: release 750 ms
 *    (was 1.5 s), decay 1.5 s (was 2.4; the SID has no 1.2 s step, and 1.5 is the
 *    nearer of its neighbours). */
static const unsigned char chime_rising[] = {
    QUIET,
    W(0, V1 + PW_HI, 0x08), W(0, V2 + PW_HI, 0x08), W(0, V3 + PW_HI, 0x08),
    W(0, V1 + AD, 0x0A), W(0, V1 + SR, 0x09),
    W(0, V2 + AD, 0x0A), W(0, V2 + SR, 0x09),
    W(0, V3 + AD, 0x0A), W(0, V3 + SR, 0x09),
    NOTE(0, V1, C5), NOTE(0, V3, C4),
    W(0, V1 + CTRL, 0x41), W(0, V3 + CTRL, 0x41),
    W(12, V3 + CTRL, 0x40),                         /* drop the gate to re-strike */
    NOTE(1, V2, E5), NOTE(0, V3, E4),
    W(0, V2 + CTRL, 0x41), W(0, V3 + CTRL, 0x41),
    W(30, V1 + CTRL, 0x40), W(0, V2 + CTRL, 0x40), W(0, V3 + CTRL, 0x40),
    TIDY(45)
};

static const unsigned char *const chimes[5] = {
    chime_chord, chime_arpeggio, chime_bell, chime_swell, chime_rising
};
static const unsigned char chime_size[5] = {
    sizeof chime_chord, sizeof chime_arpeggio, sizeof chime_bell, sizeof chime_swell,
    sizeof chime_rising
};
static const char *const chime_name[5] = {
    "a struck chord     C-G-E on three pulse waves, left to ring",
    "a rising arpeggio  C-E-G, then the octave held",
    "a bell, ding-dong  triangle waves, a falling third",
    "a filter swell     a sawtooth chord brightening as it fades in",
    "the bell reversed  rising C-E, in the struck chord's pulse voice",
};

/* ---- the player: the kernel's, in C ------------------------------------- */
static const unsigned char *pc;         /* the next triple; 0 = nothing playing */
static unsigned char wait;              /* frames before it is due */

static void chime_start(const unsigned char *list)
{
    pc = list;
    wait = list[0];
}

/* Once a frame. Writes every triple that is due, then counts down to the next. */
static void chime_frame(void)
{
    if (!pc) return;
    if (wait) { --wait; return; }
    do {
        SID[pc[1]] = pc[2];
        pc += 3;
        if (*pc == END) { pc = 0; return; }
        wait = *pc;
    } while (!wait);
    --wait;
}

/* ---- the screen --------------------------------------------------------- */
#define A_TEXT   0x07
#define A_TITLE  0x70
#define A_KEY    0x46
#define A_LIT    0x43
#define A_WARN   0x41

static void say(unsigned char col, unsigned char row, unsigned char attr, const char *p)
{
    vattr(attr);
    vaddr((unsigned int)row * 80 + col);
    while (*p) vputc((unsigned char)*p++);
}

static void say_num(unsigned char col, unsigned char row, unsigned char n)
{
    char buf[4];
    unsigned char i = 0;
    if (n >= 100) buf[i++] = (char)('0' + n / 100);
    if (n >= 10)  buf[i++] = (char)('0' + n / 10 % 10);
    buf[i++] = (char)('0' + n % 10);
    buf[i] = 0;
    say(col, row, A_TEXT, buf);
}

static void row(unsigned char i, unsigned char lit)
{
    char key[2];
    key[0] = (char)('1' + i); key[1] = 0;
    say(4, 6 + 2 * i, lit ? A_LIT : A_KEY, key);
    say(8, 6 + 2 * i, lit ? A_LIT : A_TEXT, chime_name[i]);
    say_num(72, 6 + 2 * i, chime_size[i]);
}

static void screen(void)
{
    unsigned char i;

    vattr(A_TEXT);
    vfill(' ');
    vcmd(VCMD_CLEAR);
    vhidecur();
    say(0, 0, A_TITLE, " CHIMES: candidates for the boot chime                                          ");
    say(2, 2, A_TEXT, "Each is a list of timed SID writes, played the way the kernel will play it.");
    say(70, 4, A_KEY, "bytes");
    for (i = 0; i < 5; i++) row(i, 0);
    say(4, 18, A_KEY, "Q");  say(8, 18, A_TEXT, "quit");
}

void main(void)
{
    int key;
    unsigned char lit = 0xFF;

    screen();
    if (!SOUND_ENABLE)
        say(2, 3, A_WARN, "System sound is muted, so these will be silent.");
    while (INCH_NB() >= 0) ;

    for (;;) {
        wait_frame();
        chime_frame();
        if (!pc && lit != 0xFF) { row(lit, 0); lit = 0xFF; }

        key = INCH_NB();
        if (key < 0) continue;
        if (key == 'q' || key == 'Q') break;
        if (key >= '1' && key <= '5' && SOUND_ENABLE) {
            if (lit != 0xFF) row(lit, 0);
            lit = (unsigned char)(key - '1');
            row(lit, 1);
            chime_start(chimes[lit]);
        }
    }

    SID[V1 + CTRL] = 0; SID[V2 + CTRL] = 0; SID[V3 + CTRL] = 0;
    SID[RES_FILT] = 0;
    SID[MODE_VOL] = 0x0F;           /* no filter, full volume: as the kernel expects */
    vattr(0x02);
    vfill(' ');
    vcmd(VCMD_CLEAR);
    QUITDOS();
}
