/* ============================================================================
 * The Sunless Vault -- shared declarations for the MFC text roguelike.
 *
 * The code is split into cohesive modules, each owning its own state:
 *   glue.s    platform (VIC video, keyboard, FAT16, RTC)  -- see externs below
 *   map.c     the dungeon grid + rooms; generation + field of view
 *   draw.c    screen rendering + the status/message rows
 *   player.c  the hero: stats, leveling, movement
 *   monster.c creatures: spawning + AI
 *   combat.c  actor-vs-actor resolution (to-hit, damage) -- knows no actor types
 *   vault.c   main loop, input, RNG, the turn message log
 *
 * Each module defines the globals it owns; this header declares them `extern`
 * for the others. cc65 links the translation units into one flat $0800 .PRG.
 * ==========================================================================*/
#ifndef VAULT_H
#define VAULT_H

/* ---- platform (glue.s) ---- */
extern unsigned char INCH(void);
extern int           INCH_NB(void);
extern void          QUITDOS(void);
extern void          vaddr(unsigned int cell);
extern void          vputc(unsigned char ch);
extern void          vattr(unsigned char a);
extern void          vfill(unsigned char ch);        /* fill char for chip block ops */
extern void          vcmd(unsigned char cmd);         /* chip-side clear / fill-row */
extern void          vhidecur(void);
extern unsigned int  rng_seed(void);                  /* RTC-derived RNG entropy */
extern unsigned char rtc_sec(void);                   /* BCD seconds; tested for change */

#define VCMD_CLEAR   0x01
#define VCMD_FILLROW 0x04

/* ---- screen layout: rows 0..22 map, 23 message, 24 status ---- */
#define MAP_W    80
#define MAP_H    23
#define MSG_ROW  23
#define STA_ROW  24

/* ---- tiles ---- */
#define T_WALL   0
#define T_FLOOR  1
#define T_STAIRS 2
#define T_SHRINE 3    /* an altar: stand on it and (p)ray to spend gold */
#define T_ORB    4    /* the Shimmering Orb on L15; step on it to lift it */

/* ---- colour attributes ([R:7][BR:6][bg:5-3][fg:2-0]) ---- */
#define A_WALL   0x47   /* bright white */
#define A_FLOOR  0x02   /* green */
#define A_DIM    0x40   /* dark gray (bright black) -- explored but not currently visible */
#define A_PLAYER 0x43   /* bright yellow */
#define A_MON    0x41   /* bright red */
#define A_STAIRS 0x46   /* bright cyan */
#define A_TEXT   0x07   /* white */

#define MAX_MON   16
#define MAX_ROOMS 14
#define FOV_R     8

/* ---- map.c: the dungeon grid + rooms + FOV ---- */
extern unsigned char gmap[MAP_H][MAP_W];    /* terrain */
/* visibility packed 1 bit each to save a full grid: bit0 = ever seen (drawn dim),
 * bit1 = currently in view. */
#define VSEEN 1
#define VVIS  2
extern unsigned char vseen[MAP_H][MAP_W];
/* one "entity" grid instead of separate monster/item grids (a cell holds at most
 * one): 0 = empty; 1..MAX_MON = monster index+1; >MAX_MON = MAX_MON + itemindex+1. */
extern unsigned char ent[MAP_H][MAP_W];
extern unsigned char rcx[MAX_ROOMS], rcy[MAX_ROOMS], nrooms;   /* room centres */
extern unsigned char litx0, lity0, litx1, lity1;               /* current lit box */
void gen_level(void);
void light(void);
void random_floor(signed char *ox, signed char *oy);   /* a random walkable cell */

/* ---- draw.c: rendering ---- */
void cls(void);                              /* clear the whole screen */
void put_cell(unsigned char g, unsigned char a);
void put_str(unsigned char x, unsigned char y, const char *s, unsigned char a);
void put_num(unsigned char x, unsigned char y, int v, unsigned char a);
/* draw a "a) text" lettered menu row; shared by the inventory/spell/shrine menus */
void menu_row(unsigned char idx, unsigned char lcol, unsigned char tcol,
              unsigned char row, const char *text, unsigned char a);
void render(unsigned char full);
void draw_seal(void);                        /* redraw the escape timer on the status row */

/* ---- combat.c: actor-vs-actor resolution ----
 * A Combatant is a snapshot of the numbers that drive one exchange, filled in by
 * the attacker's / defender's own module from its stats (and later gear/buffs).
 * The resolver never asks "is this the hero or a beast" -- it only sees numbers. */
struct Combatant {
    const char   *name;      /* for the log */
    unsigned char acc;       /* accuracy: raises the attacker's hit chance */
    unsigned char eva;       /* evasion: lowers the attacker's hit chance */
    unsigned char dmin, dmax;/* damage range before armour */
    unsigned char armor;     /* flat damage reduction */
};
struct AtkResult { unsigned char hit; unsigned char crit; int dmg; };
void resolve_attack(const struct Combatant *atk, const struct Combatant *def,
                    struct AtkResult *out);

/* ---- data.c: content tables ----
 * A monster type is a row of numbers + a couple of behaviour bits. monster.c
 * spawns by depth and hands these to combat.c; draw.c reads glyph/attr. Adding
 * a creature is a table row, not code. */
#define AB_REGEN   0x01    /* heals a little each turn */
#define AB_POISON  0x02    /* a landed hit poisons the hero */
#define AB_RANGED  0x04    /* (reserved for a later step) */
#define AB_SUMMON  0x08    /* raises fresh creatures on nearby cells (necromancer) */
#define AB_STUN    0x10    /* a landed hit may paralyze the hero for a turn (basilisk) */
#define AB_DRAIN   0x20    /* a hit drains life: heals the attacker + lingers (vampire) */
#define AB_MULTI   0x40    /* strikes 1-3 times per turn (hydra) */
#define AB_TELE    0x80    /* (reserved) */
#define AB_CONFUSE 0x100   /* a hit may confuse the hero: movement goes random (mind flayer) */
struct MonDef {
    unsigned char glyph, attr;
    const char   *name;
    unsigned char hp;                        /* base = max hit points */
    unsigned char acc, eva, dmin, dmax, armor;
    unsigned char xp;
    unsigned char dlo, dhi;                  /* depth range this type appears on */
    unsigned int  abil;                      /* ability bitmask (AB_*) -- 16-bit for headroom */
};
extern const struct MonDef mondef[];
extern const unsigned char nmondef;
#define MON_SKELETON 14    /* roster index of the skeleton (what the necromancer raises) */
#define MON_GUARDIAN 22    /* roster index of the L15 Orb guardian (dlo=99: never random) */

/* item kinds (the effect when used) */
#define IT_HEAL   0   /* restore HP */
#define IT_MANA   1   /* restore mana */
#define IT_MAP    2   /* reveal the whole level */
#define IT_GOLD   3   /* currency: adds to the gold purse, not the pack */
#define IT_WEAPON 4   /* permanent +1 to weapon bonus (attack) */
#define IT_ARMOR  5   /* permanent +1 to armor bonus (defense) */
struct ItemDef {
    unsigned char glyph, attr;
    const char   *name;
    unsigned char kind, mag;                 /* effect id + magnitude */
    unsigned char weight;                    /* relative drop frequency */
};
extern const struct ItemDef itemdef[];
extern const unsigned char nitemdef;

/* spells: INT-gated, mana-costed, castable from the 'c' menu. Time Stop is the
 * capstone -- its "cost" is a minimum-mana threshold and it consumes ALL mana. */
#define SP_MISSILE  0    /* bolt the nearest visible creature */
#define SP_HEAL     1    /* restore HP */
#define SP_LIGHT    2    /* reveal the whole level */
#define SP_SHIELD   3    /* temporary armour */
#define SP_TELE     4    /* blink to a random floor cell */
#define SP_TIMESTOP 5    /* freeze monsters (and, in Phase 4, the seal clock) */
struct SpellDef {
    const char   *name;
    unsigned char cost;                      /* mana (for Time Stop: min threshold) */
    unsigned char minint;                    /* INT required to know the spell */
    unsigned char kind;
};
extern const struct SpellDef spelldef[];
extern const unsigned char nspelldef;

/* ---- player.c: the hero ---- */
extern signed char   px, py;
extern int           php, pmaxhp, pmana, pmaxmana;
extern int           ppoison;                /* remaining poison ticks */
extern int           pdrain;                 /* turns a vampire's drain link is live (damage taken heals it) */
extern int           pstun;                  /* turns the hero is paralyzed (basilisk gaze) */
extern int           pstunimm;               /* turns immune to re-stun (so a stun can't chain-lock you) */
extern int           pconfuse;               /* turns the hero's movement is randomized (mind flayer) */
extern int           pgold;                  /* gold purse */
extern unsigned char porb;                   /* 1 once the Shimmering Orb is lifted */
extern unsigned char pweapon, parmor;        /* permanent gear bonuses (Yacor-style) */
extern unsigned char pshield, ptimestop, plight;   /* temporary spell effects (turns left) */
extern unsigned char pstr, pint, pcon, pdex, plevel;
extern int           pxp, pxpnext;           /* XP so far / next-level threshold (status bar) */
extern char          pname[13];
unsigned char roll3d6(void);
void          char_begin(void);                 /* fresh level-1 hero from rolled stats */
void          gain_xp(int amt);
void          debug_buff(void);                  /* DEBUG: godlike hero (formula-consistent) */
void          player_combatant(struct Combatant *c);
void          player_tick(void);                 /* per-turn upkeep: effects + regen */
void          player_bless(unsigned char which); /* shrine boon: +1 to stat 0=S 1=I 2=C 3=D */
unsigned char try_move(signed char dx, signed char dy);   /* 1 if the hero moved */

/* ---- monster.c: creatures ---- */
/* `carry` holds the entity value of a floor item the monster is standing on
 * (0 if none) so it can walk over items without destroying them -- the item is
 * put back on the cell it leaves or dies on. */
struct Mon { signed char x, y; int hp; unsigned char alive, type, carry; };
struct Mon   *mon_at(signed char x, signed char y);
void          mon_combatant(struct Mon *m, struct Combatant *c);
void          spawn_monsters(void);
void          mon_turn(void);
unsigned char occ_type(unsigned char oc);   /* type of the monster at occ value oc */
struct Mon   *nearest_vis_mon(void);         /* closest visible live creature, or 0 */
unsigned char mon_hurt(struct Mon *m, int dmg);   /* apply damage; 1 if it died */
void          player_take(int dmg);          /* damage the hero (routes to a draining vampire) */

/* ---- spell.c ---- */
unsigned char spell_screen(void);            /* the 'c' menu; 1 if a turn passed */

/* ---- item.c: floor items + inventory ---- */
#define MAX_FITEM 8
#define MAX_INV   16
unsigned char iocc_type(unsigned char io);   /* item type at item index+1 (for the renderer) */
void          spawn_items(void);
void          try_pickup(void);              /* pick up whatever the hero stands on */
unsigned char inventory_screen(void);        /* the 'i' menu; 1 if a turn passed */
unsigned char shrine_menu(void);             /* the 'p' altar menu; 1 if a turn passed */

/* ---- vault.c: core (RNG, messages, shared progress) ---- */
#define SEAL_SECONDS 600              /* real-time escape deadline once the Orb is lifted */
extern int           depth;
extern int           seal_left;       /* escape: seconds until the vault seals */
/* score tallies (round-trip depth is computed from score_deep + current depth) */
extern int           score_dmg, score_heal, score_mana, score_kills, score_deep;
extern int           score_gold;      /* gross gold collected (not the spendable purse pgold) */
extern char          msg[80];
unsigned int  rnd16(void);
unsigned char rndn(unsigned char n);
signed char   sgn(int v);
void          msg_clear(void);
void          msg_add(const char *s);
void          msg_num(int v);                /* append a decimal number to the log */
signed char   utoa(int v, char *buf);        /* int -> reversed ASCII digits; count */
void          set_msg(const char *s);

#endif /* VAULT_H */
