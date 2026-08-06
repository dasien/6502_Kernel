/* ============================================================================
 * frontier.h -- FRONTIER FORTUNE: shared constants and declarations.
 * See DESIGN.md for the game design; this header is the engine contract.
 * ==========================================================================*/
#ifndef FRONTIER_H
#define FRONTIER_H

/* ---- runtime glue (glue.s) ---- */
extern unsigned char INCH(void);                      /* blocking key read */
extern int           INCH_NB(void);
extern void          QUITDOS(void);
extern void          vaddr(unsigned int cell);
extern void          vputc(unsigned char ch);
extern void          vattr(unsigned char a);
extern void          vfill(unsigned char ch);
extern void          vcmd(unsigned char cmd);
extern void          vhidecur(void);
extern void          vshowcur(unsigned int cell);
extern unsigned int  rng_seed(void);
extern unsigned char dopen_read(const char *name);    /* 0 = ok */
extern unsigned char dopen_write(const char *name);   /* 0 = ok */
extern int           dgetb(void);                     /* byte, or -1 at EOF */
extern unsigned char dputb(unsigned char c);          /* 0 = ok */
extern void          dclose(void);

/* ---- high scores ----
 * Kept beside the game in whatever drawer it was launched from. The magic and
 * version bytes mean a file from a future format is ignored rather than
 * misread -- a corrupt table should cost you the table, never the run. */
#define SCORE_FILE   "FRONTIER.SCO"
#define SCORE_MAGIC  'F'
#define SCORE_VER    1
#define NSCORES      8
#define NAMELEN      11

#define VCMD_CLEAR   0x01

/* ---- money ----
 * MUST be 32-bit. 100 units of Gold at $29,999 is about $3,000,000, and cc65's
 * int caps at 65,535 -- using int here would silently overflow every cash, debt
 * and transaction-total path in the game. Individual prices still fit in an
 * unsigned int (max $29,999), so only totals need the wider type. */
typedef long Money;

/* ---- screen ---- */
#define SCR_W        80
#define SCR_H        25

/* ---- attributes: [R][BR][bg:3][fg:3]; 0x40 = bright ---- */
#define A_TEXT       0x07       /* white -- body text */
#define A_DIM        0x40       /* dark gray -- labels, rules */
#define A_TITLE      0x43       /* bright yellow -- headings */
#define A_CASH       0x42       /* bright green -- money you have */
#define A_DEBT       0x41       /* bright red -- money you owe */
#define A_KEY        0x46       /* bright cyan -- menu accelerator letters */
#define A_WARN       0x41       /* bright red -- warnings */
#define A_GOOD       0x47       /* bright white -- goods list */

/* ---- CP437 glyphs (decimal) ---- */
#define G_HBAR       196        /* single horizontal rule */
#define G_TL         218        /* box corners */
#define G_TR         191
#define G_BL         192
#define G_BR         217
#define G_VBAR       179
#define G_CHEAP      25         /* down arrow -- this town is abundant in it */
#define G_DEAR       24         /* up arrow -- this town is short of it */

/* ---- input ---- */
#define K_ENTER      13
#define K_BACKSP     8
#define K_ESC        27

/* ---- the run ---- */
#define NGOODS       8
#define NTOWNS       6
#define START_CASH   2000L
#define START_DEBT   5500L
#define START_SPACE  100
#define TOTAL_DAYS   60
#define DEBT_DIVISOR 50         /* debt += debt/50 per day travelled == ~2% */
#define WAGON_COST   500L       /* per wagon */
#define WAGON_SPACE  20         /* units gained per wagon */

/* A personal sidearm, bought once at the store. Entirely separate from the Guns
 * trade good -- that is cargo you haul to sell, this is what you are carrying on
 * your hip. Its purpose is to give you the OPTION to fight when road agents stop
 * you; unarmed, a holdup simply happens to you. */
#define GUN_COST     250L

/* Borrowing more is a real option -- capital now against interest later. The cap
 * only stops the absurd; 2%/day compounding is the actual deterrent. */
#define MAX_DEBT     100000L

/* ---- the economy ----
 * Prices are PERSISTENT WORLD STATE, not rerolled on arrival. Every town holds a
 * live price for every good; it drifts back toward that town's normal each day
 * and wanders a little, and your own trading moves it immediately and visibly.
 *
 * The earlier design rerolled prices across their whole range on every arrival
 * and layered a hidden "pressure" on top. That was self-defeating: the reroll
 * noise was far larger than the effect, so the player could never perceive having
 * caused anything, and the price did not move until the next visit anyway. */
#define MARKET_DEPTH  200U      /* units of volume that move a price ~50% */
#define MEAN_REVERT   6         /* each day closes 1/6 of the gap to normal */
#define WALK_DIV      16        /* daily random walk is about +/-3% of normal */
#define PRICE_CEILING 32000U    /* leave headroom for a x2 event on top */

/* Where a town's normal price sits within a good's range. */
#define ANCHOR_EXCESS 5         /* has spare: normal at base + span/5 */
#define ANCHOR_NEED   5         /* short of it: normal at base + span - span/5 */

/* A town may be short of one good, have a surplus of another, both, or neither
 * -- and it changes now and then, so the map is worth relearning. */
#define SPEC_NONE     0xFF
#define SPEC_PLAIN    3         /* 1-in-N chance a slot is empty */
#define SPEC_SHIFT    3         /* percent chance per town per day of a reshuffle */

/* ---- the trail ---- */
#define EVENT_CHANCE  20        /* percent chance of an event per journey */
#define HOLDUP_CHANCE 12        /* percent chance of road agents, rolled separately */
#define FIGHT_WIN     55        /* percent chance of driving them off */
#define FIND_MIN      500       /* a roadside cache is worth $500..$2000 of goods, */
#define FIND_SPAN     1500      /* converted to units at the local price */
#define FIND_MAX_QTY  20

/* ---- frontier.c ---- */
unsigned int  rnd16(void);
unsigned int  rndn(unsigned int n);

#endif /* FRONTIER_H */
