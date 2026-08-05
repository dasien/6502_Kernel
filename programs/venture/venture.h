/*
 * venture.h -- VENTURE for MFC: shared constants and the runtime glue API.
 *
 * A port of Exidy's Venture (1981). See DESIGN.md for the whole design and
 * docs/VENTURE.md for the player's card; this header is just the numbers the code
 * needs in one place.
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
extern void          sound_tone(unsigned int freq);  /* SID voice 1 on */
extern void          sound_off(void);                /* SID voice 1 off */

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

/* ---- the dungeon map ----------------------------------------------------
 * The other half of Venture: a hall you walk between rooms, with the room
 * entrances set into it. The arcade zoomed from map to room; we switch screens,
 * which is cheaper and reads better at 80x25.
 *
 * Rooms stay BLIND from out here -- the map shows a door, never what is behind
 * it. You commit before you know, which is where the dread lives. */
#define MAP_W     56
#define MAP_H     15
#define MAP_X     ((SCR_W - MAP_W) / 2)
#define MAP_Y     5

#define MAP_START_X 28       /* the corridor between the two wall bands */
#define MAP_START_Y 7

#define T_DOOR0   4          /* T_DOOR0 + n is the door into room n */
#define ROOMS_PER_LEVEL 4
#define LEVELS    3

#define G_DOOR    0xFE       /* a filled block set into the hall wall */
#define G_CLEARED 0xFA       /* a looted room's doorway: a faint mark */
#define A_DOOR    0x43       /* bright yellow, so entrances read at a glance */
#define A_CLEARED 0x40       /* dark grey once its treasure is gone */

/* ---- Hallmonsters -------------------------------------------------------
 * Invincible, unkillable, and not really an enemy: a clock. They patrol the hall
 * and, if you dawdle in a room, one comes through the door after you. Nothing
 * you can do stops them, which is what keeps Venture from being a leisurely
 * looting exercise. */
#define MAX_HALL      3
#define G_HALLMON     0xE8   /* a hooded figure */
#define A_HALLMON     0x45   /* magenta -- shares no colour with anything killable */
#define HALL_EVERY    4      /* map: steps every Nth tick (slower than Winky) */
#define HALL_ROOM_TICKS 260  /* room: ticks of dawdling before one comes in */
#define HALL_IN_EVERY 5      /* room: how often the intruder steps */

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
 * shapes, then naming them to match -- not by picking plausible codepoints. Six
 * themes; a level draws four of them. */
#define THEMES    6
/*                     monster              treasure                          */
/* 0 SERPENT   */ /*   0x15 section sign    0x05 club      APPLES             */
/* 1 CYCLOPS   */ /*   0xE9 theta (one eye) 0x04 diamond   JEWEL              */
/* 2 SPIDER    */ /*   0x0F sun (legs)      0x09 ring      RING               */
/* 3 GOAT      */ /*   0xEA omega (horns)   0xFE block     INGOT              */
/* 4 SKELETON  */ /*   0x9D yen (ribs)      0x0A box       CHEST              */
/* 5 WRAITH    */ /*   0xE8 phi (hooded)    0x03 heart     AMULET             */

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

#define MAX_MON     6
#define ARROW_STEP  2    /* cells per tick: an arrow outruns what it is shot at */

#define LIVES_START 3

/* ---- game modes --------------------------------------------------------- */
#define MODE_MAP  0
#define MODE_ROOM 1

/* ---- sound (SID voice 1 via K_SOUND_TONE) -------------------------------
 * Cues, not a score. The one that matters is the Hallmonster: you need to know
 * it is coming without looking away from what you are doing. */
#define SND_TREASURE 1200
#define SND_KILL      500
#define SND_DEATH     150
#define SND_HALL      280   /* the approach warning */
#define SND_TICKS       8   /* how long a cue holds, in ticks */

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
