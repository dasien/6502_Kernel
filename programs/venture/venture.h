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

/* ---- screen and playfield geometry --------------------------------------
 * The playfield runs on the VIC's DOUBLE-SIZE ROWS: physical rows PLAY_ROW0,
 * PLAY_ROW0+2, ... are flagged 16x32, so each holds 40 characters instead of 80 and
 * covers two rows of screen. Glyphs come out four times the area, which is the only
 * way to make the shapes themselves bigger with a fixed 8x16 font ROM -- reverse
 * video and multi-cell bodies change ink and footprint, never the shape.
 *
 * The HUD and the status line stay on ordinary rows, so text is still crisp. That is
 * what per-row double size buys over a whole-screen mode.
 *
 * A tile is 16x32 px, so 30x11 tiles is 480x352 -- about 4:3, close to the arcade's
 * rooms, with five double-columns of bezel each side. Going the full 40 wide would
 * have made a 1.8:1 letterbox. */
#define SCR_W     80          /* the character plane is still 80 cells wide */
#define SCR_H     25

#define HUD_ROW    0          /* ordinary rows, above and below the playfield */
#define MSG_ROW   23

#define PLAY_ROW0  1          /* first physical row of the double-size band */
#define PLAY_COLS 40          /* a double row holds this many characters */

#define ROOM_W    30
#define ROOM_H    11
#define ROOM_X    ((PLAY_COLS - ROOM_W) / 2)   /* the bezel, in double-columns */
#define ROOM_Y    0

/* Setting a row's size is a command, not a register: parameter carries the row and
 * bit 7 the size. A clear puts every row back to normal, so the band has to be laid
 * out again after one -- which is why set_play_rows() sits next to every clear. */
#define VCMD_ROWSIZE  5
#define VCMD_ROWSNORM 6
#define VROW_DOUBLE   0x80

/* ---- the dungeon hall ---------------------------------------------------
 * The other half of Venture: an open arena you cross between rooms, with the four
 * rooms sitting in it as blocks. The arcade zoomed from hall to room interior; we
 * switch screens, because a 44x15 room cannot also be drawn to scale inside a
 * 56x15 hall.
 *
 * Each room is drawn as a hollow OUTLINE with its entrances notched into it, which
 * is how the arcade draws its dungeon floor. Loot it and the interior fills in and
 * the notches seal: a hollow box becomes a solid one.
 *
 * Rooms stay BLIND from out here -- the outline shows the room's footprint, never
 * what is inside it. You commit before you know, which is where the dread lives.
 * Whether a room is DONE is the one thing the hall tells you, and only after you
 * have already been in. */
#define MAP_W     ROOM_W     /* the hall is the same shape as a room */
#define MAP_H     ROOM_H
#define MAP_X     ROOM_X
#define MAP_Y     ROOM_Y

#define MAP_START_X 14       /* the middle of the open arena */
#define MAP_START_Y 5

#define ROOMS_PER_LEVEL 4
#define LEVELS    3

/* A room's outline and a looted room's fill are the same solid block, as they are
 * in the arcade -- what tells them apart is whether the inside of the box is black
 * or filled. */
#define G_SEALED  0xDB       /* == G_WALL; see above */
#define G_DOOR    0xFE       /* an entrance, while there is a reason to use it */
#define A_DOOR    0x43       /* bright yellow, so entrances read at a glance */

/* ---- Hallmonsters -------------------------------------------------------
 * Invincible, unkillable, and not really an enemy: a clock. They patrol the hall
 * and, if you dawdle in a room, one comes through the door after you. Nothing
 * you can do stops them, which is what keeps Venture from being a leisurely
 * looting exercise.
 *
 * Their number GROWS as you clear rooms -- the arcade's hall starts nearly empty
 * and is crawling by the fourth room, which is what stops the last room of a level
 * being the easiest. The layout carries one post per possible Hallmonster;
 * HALL_BASE of them are awake at the start of a level and one more wakes per room
 * looted.
 *
 * The one that comes into a room WALKS THROUGH THE WALLS. It does not path round the
 * layout, it does not get stuck on anything, and nothing you can build between you
 * and it helps: it comes straight at you until you leave. That is the whole point of
 * it, and it is why the room's second doorway matters. */
#define MAX_HALL      6
#define HALL_BASE     1
#define G_HALLMON     0xE8   /* a hooded figure */
#define HALL_EVERY    5      /* hall: steps every Nth tick (slower than Winky) */
#define HALL_ROOM_TICKS 170  /* room: ticks of dawdling before one comes in */
#define HALL_IN_EVERY 4      /* room: how often the intruder steps */

/* ---- tiles (what is in a cell, independent of what is drawn) ------------ */
#define T_FLOOR   0
#define T_WALL    1
#define T_TREAS   2
#define T_CORPSE  3   /* a killed monster; still lethal to touch */
#define T_EXIT    4   /* room: a doorway in the border -- walk out of it */
#define T_BLK0    5   /* hall: T_BLK0 + n is room n's outline */
#define T_VOID0   9   /* hall: T_VOID0 + n is inside it -- black, or filled if done */
#define T_DOOR0  13   /* hall: T_DOOR0 + n is an entrance to room n */

/* ---- glyphs, all from the machine's CP437 ROM --------------------------- */
/* Winky is CP437 $01, an outline smiley -- the protagonist ships in the character
 * ROM. $02 is the filled version and he used to alternate between the two every
 * tick; at 16x32 that reads as a flicker rather than as animation, so he does not. */
#define G_WINKY   0x01
#define G_WALL    0xDB   /* solid block */
#define G_FLOOR   ' '
#define G_CORPSE  0xB0   /* light shade -- remains, clearly not a wall */
#define G_ARROW_U 0x18
#define G_ARROW_D 0x19
#define G_ARROW_R 0x1A
#define G_ARROW_L 0x1B
#define G_DOORWAY 0xF0   /* a room's doorway -- white, as the arcade's notches are */

/* Where the next arrow will go, shown as a dim pip in the cell Winky faces. The
 * arcade draws this and it matters here for the same reason: facing persists after
 * you let go of the key, so without it you cannot see what you are aimed at. */
#define G_FACE_U  0x1E
#define G_FACE_D  0x1F
#define G_FACE_R  0x10
#define G_FACE_L  0x11
#define A_FACE    0x03   /* dark yellow: Winky's colour, dimmed */

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

/* ---- attributes: [R][BRIGHT][bg:3][fg:3] --------------------------------
 * Walls, room blocks and Hallmonsters are recoloured every level, as the arcade
 * recolours the whole dungeon: magenta, then cyan, then yellow. The tables are in
 * venture.c; these are the fixed ones. */
#define A_TEXT    0x02   /* green on black, the machine's default */
#define A_HUD     0x47   /* bright white */
#define A_WINKY   0x43   /* bright yellow, as the arcade smiley was */
#define A_MON     0x41   /* bright red */
#define A_CORPSE  0x40   /* bright black == dark grey */
#define A_DOORWAY 0x47   /* bright white */
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
 * per-entity speed constants anywhere.
 *
 * Six rather than four since the playfield went to double-size rows. A step used to
 * cover 8 px across and 16 down; it now covers 16 and 32, so at the old rate
 * everything crossed the screen at twice the speed while having a third less room to
 * cross it in. Ten ticks a second puts a room-crossing back at about three seconds,
 * which is where it was. It also leaves more headroom in the ramp, since the loop
 * decrements this and it can now come down further before bottoming out. */
#define TICK_RATE   6    /* 60/6 = 10 ticks per second */
#define MAX_CATCHUP 4    /* simulation steps per pass, so a stall cannot spiral */

/* Monsters step every Nth tick, which is how they end up slower than Winky without a
 * second clock. Four rather than three: a smaller room leaves less space to
 * out-manoeuvre anything, so the same relative speed reads as more pressure than it
 * used to. The Hallmonster constants keep their old relationship to this -- ones
 * patrolling the hall slower than room monsters, an intruder exactly as fast. */
#define MON_EVERY   4

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

/* ---- the level bonus ----------------------------------------------------
 * The arcade tallies SCORE THIS LEVEL x BONUS MULTIPLIER between levels; the
 * screenshot shows a x6. What sets the multiplier is not visible from a still, so
 * this is our rule: the level you just finished plus the lives you still have. It
 * reaches x6 exactly where the arcade's shot does (level 3, three lives) and it
 * makes not dying worth something beyond not dying. */
#define BONUS_MULT(lvl, liv) ((unsigned char)((lvl) + (liv)))

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
