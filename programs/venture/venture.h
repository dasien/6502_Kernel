/*
 * venture.h -- VENTURE for MFC: shared constants and the runtime glue API.
 *
 * A port of Exidy's Venture (1981). See DESIGN.md for the whole design; this
 * header is just the numbers the code needs in one place.
 *
 * Build steps 1-5 of that plan: one playable room. The dungeon map, the
 * Hallmonsters, the other eleven rooms and the level loop come after.
 */

#ifndef VENTURE_H
#define VENTURE_H

/* ---- runtime glue (glue.s) ---------------------------------------------- */
extern int           INCH_NB(void);            /* next key, or -1 if none ready */
extern void          QUITDOS(void);
extern void          vaddr(unsigned int cell); /* point the data port at a cell */
extern void          vputc(unsigned char ch);  /* write a glyph; auto-increments */
extern void          vattr(unsigned char a);   /* colour latch for later writes */
extern void          vfill(unsigned char ch);  /* fill char for clear/fill-row */
extern void          vcmd(unsigned char cmd);  /* chip-side block op */
extern void          vhidecur(void);           /* hide the kernel's cursor */
extern unsigned int  rng_seed(void);           /* RTC-derived entropy */
extern unsigned char rtc_sec(void);            /* BCD seconds */
extern unsigned int  jiffies(void);            /* 60 Hz monotonic counter */
extern unsigned char keystate(void);           /* live held-key mask ($FE0F) */

#define VCMD_CLEAR 1

/* ---- control port bits ($FE0F) ------------------------------------------ */
#define KS_UP     0x01
#define KS_DOWN   0x02
#define KS_LEFT   0x04
#define KS_RIGHT  0x08
#define KS_FIRE   0x10
#define KS_BTN2   0x20

/* ---- screen and room geometry ------------------------------------------- */
#define SCR_W     80
#define SCR_H     25

#define HUD_ROW    1
#define MSG_ROW   22

/* The room is drawn centred, with the HUD above and a message line below. Room
 * cells are addressed 0..ROOM_W-1 / 0..ROOM_H-1 and offset to the screen when
 * drawn, so all the game logic works in room coordinates. */
#define ROOM_W    44
#define ROOM_H    15
#define ROOM_X    ((SCR_W - ROOM_W) / 2)
#define ROOM_Y    5

/* ---- room tiles (what is in a cell, independent of what is drawn) ------- */
#define T_FLOOR   0
#define T_WALL    1
#define T_TREAS   2
#define T_CORPSE  3   /* a killed monster; still lethal to touch */

/* ---- glyphs, all from the machine's CP437 ROM --------------------------- */
/* Winky is $01/$02 -- an outline smiley and a filled one, so the protagonist
 * and his two-frame animation ship in the character ROM. */
#define G_WINKY   0x01
#define G_WINKY_2 0x02
#define G_WALL    0xDB   /* solid block */
#define G_FLOOR   ' '
#define G_CORPSE  0xB0   /* light shade -- remains, clearly not a wall */
#define G_ARROW_U 0x18
#define G_ARROW_D 0x19
#define G_ARROW_R 0x1A
#define G_ARROW_L 0x1B

/* Monsters and treasures were chosen by rendering the ROM and looking at the
 * shapes, then naming them to match -- not by picking plausible codepoints. */
#define G_SERPENT 0x15   /* section sign: an S-curve */
#define G_TREAS_A 0x05   /* club: a cluster -- APPLES */

/* ---- attributes: [R][BRIGHT][bg:3][fg:3] -------------------------------- */
#define A_TEXT    0x02   /* green on black, the machine's default */
#define A_HUD     0x47   /* bright white */
#define A_WALL    0x06   /* cyan */
#define A_WINKY   0x43   /* bright yellow, as the arcade smiley was */
#define A_MON     0x41   /* bright red */
#define A_CORPSE  0x40   /* bright black == dark grey */
#define A_TREAS   0x47   /* bright white */
#define A_ARROW   0x42   /* bright green */

/* ---- pacing -------------------------------------------------------------
 * Straight from KERNEL PANIC, which has proved it on this hardware: a fixed-tick
 * accumulator off jiffies(), keystate() sampled once per tick, and one cell of
 * movement per tick per held direction bit. Movement is therefore exactly as
 * smooth as the tick rate, and because the port's bits are independent there is
 * no "most recent key wins" to fight -- steering while firing is free.
 *
 * TICK_RATE is jiffies per tick, so smaller is faster. It is also the difficulty
 * ramp: later loops lower it and the whole world speeds up together, with no
 * per-entity speed constants anywhere. */
#define TICK_RATE   4    /* 60/4 = 15 ticks per second */
#define MAX_CATCHUP 4    /* simulation steps per pass, so a stall cannot spiral */

/* Monsters step every Nth tick, which is how they end up slower than Winky
 * without a second clock. */
#define MON_EVERY   3

#define MAX_MON     5
#define ARROW_STEP  2    /* cells per tick: an arrow outruns what it is shot at */

#define LIVES_START 3

/* ---- scoring ------------------------------------------------------------
 * The original's rule, kept exactly: the treasure is worth 200 x level, and a
 * monster is worth 100 x level ONLY once the treasure is in hand -- zero before.
 * That inverts the safe instinct to clear the room and then loot, which is the
 * whole risk system in one line. */
#define SCORE_TREASURE 200
#define SCORE_MONSTER  100

/* ---- helpers in venture.c ---------------------------------------------- */
unsigned int  rnd16(void);
unsigned char rndn(unsigned char n);

#endif /* VENTURE_H */
