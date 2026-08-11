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
/* Sprites. Pixel-positioned and, crucially, NOT riding the scroll region -- the only
 * place a screen-fixed object can live once the world scrolls in sub-cell steps.
 * Select a sprite, then set its fields; positions are given in CELLS and the glue
 * converts to the chip's nominal pixels.
 *
 * Sprite 0 is the craft; 1..MAX_SHOTS are the shots. Both were juddering in the cell
 * plane for the same reason, and both are cured the same way. */
extern void          spr_sel(unsigned char index);
extern void          spr_x(unsigned char cell_col);
extern void          spr_y(unsigned char cell_row);
extern void          spr_glyph(unsigned char glyph);
extern void          spr_attr(unsigned char attr);
extern void          spr_on(unsigned char enable);

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
#define VCMD_FINEY      0x0C    /* param = pixels to slide the scroll region down */

/* ---- fine scroll ----
 * The chip can slide the scroll region down by a pixel count, so the world moves in
 * sub-cell steps and a real row-scroll only happens when the offset wraps. One row
 * of terrain is therefore FINE_STEPS visual frames instead of one jump.
 *
 * The region's top row becomes a hidden staging row: row 0 -- which is where gen_row()
 * already writes the newest row -- sits above the visible area and slides into view.
 * That fits this game exactly; nothing had to be restructured for it.
 *
 * Known cost: the offset moves EVERYTHING in the region, so the terrain, the nodes,
 * the enemies and the pellets all ride it correctly -- but the craft and your shots are
 * screen-referenced and will sawtooth by one cell per row. Fixing those two needs
 * sprites. Press F to hide just those two and confirm the conduit itself is smooth. */
#define CELL_H          16      /* pixel height of an ordinary row */
#define FINE_PX         2       /* pixels per visual step */
#define FINE_STEPS      (CELL_H / FINE_PX)

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
 * NOTE this now paces a VISUAL step (FINE_PX pixels), not a whole row: a row of world
 * takes FINE_STEPS of them. 1 jiffy per step = 60 steps/sec = 7.5 rows/sec, near the
 * 8.5 rows/sec the whole-cell version ran at, so speed is comparable and only the
 * smoothness differs. */
#define TICK_DEFAULT 1
#define TICK_MIN     1          /* 60 visual steps/sec = 7.5 rows/sec */
#define TICK_MAX     6          /* 10 visual steps/sec = 1.25 rows/sec */
#define MAX_CATCHUP  8          /* visual steps per pass before we resync to now.
                                 * A whole row's worth, so a slow frame can still
                                 * complete the row it is partway through. */

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

/* One sprite per shot plus one for the craft, and the chip has 17 -- so 16. In play
 * that is generous: the plain gun keeps about 3 in the air, spread level 2 about 9.
 * Only spread level 3 under sustained OVERCLOCK wants more (~20), and reserving for
 * that would have eaten nearly the whole I/O page for a case that barely happens.
 *
 * A full pool must never change the weapon's SHAPE, though, and this is the trap: fire()
 * spawns the centre shot first and works outwards, so dropping individual shots eats the
 * WINGS and level 3 spread quietly renders as level 1 at exactly the moment you
 * overclocked to get it. So a volley is ALL OR NOTHING -- if the whole thing does not
 * fit, nothing fires and the shot is retried next tick. Saturation costs you rate,
 * which is legible, instead of silently narrowing the gun. */
#define MAX_SHOTS        16
#define SHOT_SPEED       2      /* screen rows per tick, substepped so it can't tunnel */
#define FIRE_COOLDOWN    3      /* ticks between shots */
#define FIRE_COOLDOWN_OC 2      /* ... while overclocked. 1 put ~27 shots in the air at
                                 * spread level 3, which overflowed a 24 pool and ate
                                 * the wings; 2 keeps the peak near 18. */

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
#define A_FOE       0x41        /* bright red -- the diving daemon */
#define A_FOE2      0x45        /* bright magenta -- the weaving worm */
#define A_FOE3      0xC1        /* REVERSED bright red -- the emplaced sentinel: a solid
                                 * red block with the diamond knocked out of it. All six
                                 * bright hues were already spoken for, so the sentinel
                                 * is distinguished by inverting rather than by another
                                 * colour -- and inverted reads as "emplaced", which
                                 * suits the one enemy that holds station and shoots. */
#define A_NODE      0x42        /* bright green -- data node (matches the energy bar,
                                 * and stays clear of craft yellow / wall cyan) */
#define A_FRAG      0x67        /* bright white on BLUE -- a weapon fragment. Was
                                 * reversed bright blue, which is near-black on this
                                 * palette and effectively invisible: blue was "the one
                                 * colour nothing had claimed" precisely because it is
                                 * the one that does not read against a dark field.
                                 * White ink on a blue tile is legible instead, and
                                 * still nothing else on screen looks like it. */
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
