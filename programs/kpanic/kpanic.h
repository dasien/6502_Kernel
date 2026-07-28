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

/* The conduit: walls at LANE_L / LANE_R, open lane between them. */
#define LANE_L      18
#define LANE_R      61
#define LANE_W      (LANE_R - LANE_L - 1)   /* 42 open columns */

/* ---- timing ----
 * TICK_* are jiffies-per-simulation-step at 60 Hz: 4 -> 15 steps/sec. Difficulty
 * scales by shrinking this divisor, never by fractional speeds (cc65 has no float). */
#define TICK_DEFAULT 4
#define TICK_MIN     1          /* 60 steps/sec */
#define TICK_MAX     15         /* 4 steps/sec */
#define MAX_CATCHUP  4          /* sim steps per pass before we resync to now */

/* ---- attributes: [R][BR][bg:3][fg:3]; 0x40 = bright ---- */
#define A_WALL      0x46        /* bright cyan -- conduit wall */
#define A_STREAM    0x04        /* blue -- streaming data */
#define A_STREAM2   0x05        /* magenta -- stream accent */
#define A_CRAFT     0x43        /* bright yellow -- your trace process */
#define A_FOE       0x41        /* bright red -- corruption */
#define A_HUD       0x46        /* bright cyan -- HUD frame/labels */
#define A_TEXT      0x07        /* white -- HUD values */
#define A_WARN      0x41        /* bright red -- alerts */
#define A_DARK      0x40        /* dark gray -- outside the conduit */

/* ---- CP437 glyphs (decimal) ---- */
#define G_WALL      178         /* medium shade block */
#define G_STREAM1   176         /* light shade */
#define G_STREAM2   177         /* medium-light shade */
#define G_CRAFT     30          /* solid up triangle */
#define G_MARK      205         /* double horizontal -- scroll ruler marker */
#define G_HBAR      196         /* single horizontal -- HUD rule */

/* ---- kpanic.c ---- */
unsigned int  rnd16(void);
unsigned char rndn(unsigned char n);
signed char   utoa(unsigned int v, char *buf);   /* -> reversed digits; count */

#endif /* KPANIC_H */
