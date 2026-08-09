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
extern unsigned char keystate(void);                  /* live held-key bitmask ($FE0F) */

/* keystate() bits, active-high. Independent bits are the whole point: holding a
 * direction and firing are simultaneous by construction. */
#define KS_UP       0x01
#define KS_DOWN     0x02
#define KS_LEFT     0x04
#define KS_RIGHT    0x08
#define KS_FIRE     0x10        /* Space */
#define KS_BOOST    0x20        /* Left Shift -- hold to OVERCLOCK */

/* ---- VIC command codes ---- */
#define VCMD_CLEAR      0x01
#define VCMD_SCROLLUP   0x02
#define VCMD_SCROLLDOWN 0x03    /* content moves down; row 0 opens (filled) */
#define VCMD_FILLROW    0x04

/* ---- screen geometry ----
 * Ordinary single-size rows: 80 columns, playfield rows 0..PLAY_LAST, HUD pinned on
 * the last line below the scroll region.
 *
 * This was briefly a band of DOUBLE-SIZE rows (40x12, glyphs at 16x32) to make the
 * shapes bigger. It did that, and it made the game worse, because the scroll quantum
 * is one cell: at double size every step is a 32 px jump, which reads as a strobe
 * rather than motion. Single rows halve the quantum to 16 px AND double the runway
 * from 12 rows to 24 -- about the same pixel distance per second, but twice the
 * granularity and twice as many decision points.
 *
 * Bigger glyphs turned out not to be what made the game hittable anyway. Two-cell
 * enemy bodies and span-based collision did that, and both survive at single size
 * (a two-cell body is 16x16 px -- square, and twice the area of one cell).
 *
 * PROW() stays as an identity macro rather than being deleted: it marks every place
 * that converts a playfield row to a screen row, which is exactly what has to change
 * if the band ever moves again. */
#define SCR_W       80
#define SCR_H       25
#define PLAY_COLS   80          /* the playfield is the full width again */
#define PLAY_H      24          /* playfield rows 0..23 */
#define BAND_BOT    23          /* bottom of the band = scroll-region bottom */
#define PLAY_LAST   (PLAY_H - 1)
#define PROW(r)     ((unsigned int)(r))         /* playfield row -> screen row */

/* ONE ordinary row of HUD, on the last line of the screen. It was three, which put
 * the craft five physical rows clear of the bottom and made it look like it was
 * flying in the middle of the screen. The key legend those rows carried is now on
 * the title screen where it belongs, and the band grew from 11 logical rows to 12
 * -- so this bought back runway as well as seating the craft where it belongs. */
#define HUD_ROW     24

/* The conduit is generated per row as a center column +/- a half-width, so it
 * meanders and squeezes naturally (two independent wall walks are much harder to
 * keep sane). Walls must stay on screen, and the channel has to stay wide enough
 * to dodge in. A wall may not sit at column 0
 * or 79, because the bevel one cell outside it has to stay on screen. */
#define WALL_MIN_X  2           /* leftmost a wall may sit */
#define WALL_MAX_X  77          /* rightmost a wall may sit */
#define HW_MIN      4           /* half-width floor -> 7 open columns */
#define HW_MAX      21          /* half-width ceiling -> 41 open columns */
#define ISL_MIN_HW  12          /* islands only appear in a channel this wide */

/* ---- timing ----
 * TICK_* are jiffies-per-simulation-step at 60 Hz. Difficulty scales by shrinking
 * this divisor, never by fractional speeds (cc65 has no float).
 *
 * 7 -> ~8.5 steps/sec. At a 16 px quantum that is ~137 px/sec, roughly 30% slower
 * than the double-row band managed (32 px x 6/sec = 192 px/sec) -- and it covers the
 * ground in twice as many steps, which is the whole point of the revert. */
#define TICK_DEFAULT 7
#define TICK_MIN     2          /* 30 steps/sec */
#define TICK_MAX     30         /* 2 steps/sec */
#define MAX_CATCHUP  4          /* sim steps per pass before we resync to now */

/* Steering reads the PIA's live key-state port once per tick, so movement is
 * exactly as smooth as the tick rate and firing is independent of it. This
 * replaces a pile of heuristics that tried to infer "key still held" from the
 * keystroke stream -- impossible, because that stream has no key-up and the host
 * only auto-repeats the most recent key, which is why you could never move and
 * fire at the same time. */
#define MOVE_PER_TICK 1         /* columns per tick while a direction is held */

/* ---- the shared ENERGY pool (the signature mechanic; see DESIGN.md) ----
 * One number is simultaneously your fuel, your clock, and your ammo budget: it
 * drains on its own so idling is never safe, refills only from data nodes, and
 * OVERCLOCK spends it hard. Every aggressive choice is bought with lifespan. */
#define ENERGY_MAX      1000
#define ENERGY_DRAIN    2       /* per tick, unconditionally */
#define ENERGY_OC_DRAIN 8       /* additional per tick while overclocked */
#define ENERGY_NODE     350     /* refill for taking a node instead of shooting it */
#define ENERGY_CRASH    150     /* cost of hitting the conduit */
#define ENERGY_LOW      250     /* below this the bar goes red and the HUD warns */
#define NODE_W          2       /* node width in cells -- one cell was too fine a
                                 * target to line up on while dodging */

/* ---- weapon ----
 * Colour-coded pickups swap the gun's character; collecting the same kind again
 * deepens it. On a text display the pickup's LETTER carries the identity, not its
 * colour -- red already means corruption and green already means a data node, so
 * a third colour-coded meaning would be one too many for the eye to hold. */
#define W_PLAIN          0      /* one shot up the column */
#define W_SPREAD         1      /* S: fires into adjacent columns too */
#define W_BEAM           2      /* B: shots punch through what they hit */
#define W_HOMING         3      /* H: shots drift toward the nearest target */
#define W_MAXLEVEL       3

/* Deep enough that a spread level 3 volley always fits, even while OVERCLOCKED:
 * five shots take ~12 ticks to cross the 24-row playfield and OC fires every 2, so
 * about six volleys are in the air at once. That is why this is 32 and not 10.
 *
 * It matters more than a pool size usually would, because fire() spawns the
 * centre shot first and works outwards -- so a full pool drops the WINGS, and
 * level 3 spread quietly renders as level 1 at exactly the moment you overclocked
 * to get it. The fire rate is the balance dial here (FIRE_COOLDOWN_OC); shrinking
 * the pool does not limit the weapon, it deforms it. */
#define MAX_SHOTS        32
#define SHOT_SPEED       2      /* screen rows per tick, substepped so it can't tunnel */
#define FIRE_COOLDOWN    3      /* ticks between shots */
#define FIRE_COOLDOWN_OC 1      /* ... while overclocked */

/* ---- corruption (enemies) ----
 * Unlike nodes, these do not ride the terrain ring: they move independently of
 * the world, so they need their own pools and explicit erase/redraw. */
#define MAX_ENEMIES     8
#define MAX_PELLETS     6

/* Corruption is TWO cells wide. On a double-size row that is a 32x32 px body --
 * an arcade-sized target instead of a 16x32 sliver. It is the single biggest thing
 * that makes the game hittable: a one-cell enemy needs the craft on exactly the
 * right column, and at that precision a miss is indistinguishable from a bug.
 * Every test against an enemy has to cover e_x AND e_x+1. */
#define ENEMY_W         2

/* Kill/impact markers. Without these a hit had no signature at all -- the target
 * simply stopped being drawn, which reads as the shot having passed through it. */
#define MAX_POPS        6
#define POP_TICKS       2

#define E_NONE          0
#define E_DAEMON        1       /* closes on you faster than the world scrolls */
#define E_WORM          2       /* rides the world, weaving across the channel */
#define E_SENTINEL      3       /* rides the world, fires aimed pellets */

#define SPAWN_MIN       18      /* ticks between spawns: MIN + rnd(VAR) */
#define SPAWN_VAR       22
#define SENTINEL_FIRE   14      /* ticks between a sentinel's shots */

#define HP_DAEMON       1
#define HP_WORM         2
#define HP_SENTINEL     3

#define ENERGY_HIT      120     /* flying into corruption */
#define ENERGY_PELLET   60      /* taking a pellet */

/* ---- scoring ----
 * unsigned int caps at 65535; step 7 widens this to two words if bosses and long
 * runs start pushing it. */
#define SCORE_NODE      25
#define SCORE_DAEMON    15
#define SCORE_WORM      25
#define SCORE_SENTINEL  40

/* ---- fragments (weapon pickups) ---- */
#define MAX_FRAGS       3
#define FRAG_CHANCE     6       /* 1-in-N chance a kill drops one */

/* ---- attributes: [R][BR][bg:3][fg:3]; 0x40 = bright ---- */
#define A_WALL      0x46        /* bright cyan -- conduit wall (the lethal edge) */
#define A_BEVEL     0x06        /* cyan -- the wall's outer bevel cell */
#define A_BOARD     0x02        /* green -- circuit-board traces outside the conduit */
#define A_RECESS    0x40        /* dark gray -- the board seen in shadow, inside the
                                 * channel: same routing as outside, just recessed */
#define A_CRAFT     0x43        /* bright yellow -- your trace process */
#define A_FOE       0x41        /* bright red -- corruption */
#define A_FOE2      0x45        /* bright magenta -- the weaving variety */
#define A_NODE      0x42        /* bright green -- data node (matches the energy bar,
                                 * and stays clear of craft yellow / wall cyan) */
#define A_FRAG      0xC4        /* bright blue, reverse video -- a weapon fragment.
                                 * Reverse makes it read as a solid chip rather
                                 * than a glyph, and blue was the one bright
                                 * colour nothing else had claimed. */
#define A_SHOT      0x47        /* bright white -- reserved for the fastest thing on
                                 * screen, so the eye tracks projectiles first */
#define A_OK        0x42        /* energy bar: healthy */
#define A_MID       0x43        /* energy bar: getting thin */
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
#define G_NODE      8           /* data node -- fly over to refill, or shoot for score */
#define G_SHOT      24          /* your projectile (up arrow: unambiguous direction) */
#define G_DAEMON    31          /* solid down triangle -- coming at you */
#define G_WORM      215         /* weaving corruption */
#define G_SENTINEL  4           /* diamond -- emplaced, shoots */
#define G_PELLET    7           /* enemy shot */
#define G_BAR_FULL  219         /* energy bar: filled cell */
#define G_BAR_EMPTY 176         /* energy bar: empty cell */
#define G_CRAFT     30          /* solid up triangle */
#define G_BOOM      15          /* sun -- placeholder impact pop (real juice: step 7) */
#define G_HBAR      196         /* single horizontal -- HUD rule */

/* ---- kpanic.c ---- */
unsigned int  rnd16(void);
unsigned char rndn(unsigned char n);
signed char   utoa(unsigned int v, char *buf);   /* -> reversed digits; count */

#endif /* KPANIC_H */
