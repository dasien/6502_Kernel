/* ============================================================================
 * kpanic.h -- KERNEL PANIC: shared constants and declarations.
 * See DESIGN.md for the game design; this header is the engine contract.
 * ==========================================================================*/
#ifndef KPANIC_H
#define KPANIC_H

/* ---- runtime glue (glue.s) ---- */
extern int           INCH_NB(void);                   /* next key, or -1 if none */
extern void          QUITDOS(void);
extern void          vaddr(unsigned int cell);
extern void          vputc(unsigned char ch);
extern void          vattr(unsigned char a);
extern void          vfill(unsigned char ch);
extern void          vcmd(unsigned char cmd);
extern void          vscrollbot(unsigned char row);   /* set AFTER a clear, not before */
extern void          vhidecur(void);
extern unsigned int  rng_seed(void);                  /* RTC-derived RNG entropy */
extern unsigned char rtc_sec(void);                   /* BCD seconds; tested for change */
extern unsigned int  jiffies(void);                   /* 60 Hz monotonic tick counter */

/* ---- VIC command codes ---- */
#define VCMD_CLEAR      0x01
#define VCMD_SCROLLUP   0x02
#define VCMD_SCROLLDOWN 0x03    /* content moves down; row 0 opens (filled) */
#define VCMD_FILLROW    0x04

/* ---- screen geometry ----
 * The playfield is the full 80 columns of rows 0..PLAY_BOT (the chip-side scroll
 * moves whole rows, so a side-panel HUD would scroll with the world). The HUD is
 * pinned on the rows below the scroll region, the way IRC pins its input line. */
#define SCR_W       80
#define PLAY_BOT    21          /* last playfield row = scroll-region bottom */
#define PLAY_H      22          /* rows 0..21 */
#define HUD_ROW     22          /* HUD occupies rows 22..24 */

/* The conduit is generated per row as a center column +/- a half-width, so it
 * meanders and squeezes naturally (two independent wall walks are much harder to
 * keep sane). Walls must stay on screen, and the channel has to stay wide enough
 * to dodge in. */
#define WALL_MIN_X  2           /* leftmost a wall may sit */
#define WALL_MAX_X  77          /* rightmost a wall may sit */
#define HW_MIN      4           /* half-width floor -> 7 open columns */
#define HW_MAX      21          /* half-width ceiling -> 41 open columns */
#define ISL_MIN_HW  12          /* islands only appear in a channel this wide */

/* ---- timing ----
 * TICK_* are jiffies-per-simulation-step at 60 Hz: 4 -> 15 steps/sec. Difficulty
 * scales by shrinking this divisor, never by fractional speeds (cc65 has no float). */
#define TICK_DEFAULT 4
#define TICK_MIN     1          /* 60 steps/sec */
#define TICK_MAX     15         /* 4 steps/sec */
#define MAX_CATCHUP  4          /* sim steps per pass before we resync to now */

/* Steering: a press always moves EXACTLY one column, so taps stay precise. The
 * host's key auto-repeat only begins after ~500ms; once it does, repeats arrive
 * every few ticks, and each is granted a short glide so the motion between them
 * reads as continuous instead of stuttering. A deliberate single tap gets no
 * glide at all -- that's the difference from a flat glide-on-every-press, which
 * made one tap slide half a dozen columns. */
#define REPEAT_WINDOW 10        /* a press this soon after the last = auto-repeat */
#define REPEAT_GLIDE  3         /* ticks of drift granted to a repeat press */

/* ---- attributes: [R][BR][bg:3][fg:3]; 0x40 = bright ---- */
#define A_WALL      0x46        /* bright cyan -- conduit wall (the lethal edge) */
#define A_BEVEL     0x06        /* cyan -- the wall's outer bevel cell */
#define A_BOARD     0x02        /* green -- circuit-board traces outside the conduit */
#define A_RECESS    0x40        /* dark gray -- the board seen in shadow, inside the
                                 * channel: same routing as outside, just recessed */
#define A_CRAFT     0x43        /* bright yellow -- your trace process */
#define A_FOE       0x41        /* bright red -- corruption */
#define A_HUD       0x46        /* bright cyan -- HUD frame/labels */
#define A_TEXT      0x07        /* white -- HUD values */
#define A_WARN      0x41        /* bright red -- alerts */
#define A_DARK      0x40        /* dark gray -- outside the conduit */

/* ---- CP437 glyphs (decimal) ---- */
#define G_WALL      219         /* full block -- the wall proper */
#define G_BEVEL     178         /* medium shade -- one cell outside the wall, so the
                                 * edge reads as a shaped lip instead of a flat slab */
#define G_TRACE_H   196         /* board: horizontal trace */
#define G_TRACE_V   179         /* board: vertical trace */
#define G_VIA       197         /* board: trace crossing */
#define G_PAD       9           /* board: solder pad */
#define G_CRAFT     30          /* solid up triangle */
#define G_BOOM      15          /* sun -- placeholder impact pop (real juice: step 7) */
#define G_HBAR      196         /* single horizontal -- HUD rule */

/* ---- kpanic.c ---- */
unsigned int  rnd16(void);
unsigned char rndn(unsigned char n);
signed char   utoa(unsigned int v, char *buf);   /* -> reversed digits; count */

#endif /* KPANIC_H */
