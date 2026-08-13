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
extern void          vfontaddr(unsigned int idx); /* point the font port at glyph*16 */
extern void          vfontput(unsigned char bits);/* write one scanline; auto-increments */
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
/* ---- sprites ------------------------------------------------------------
 * The movers are sprites, not cells. Unlike the character and font planes, the
 * sprite registers ARE in the 6502's map, so C writes them directly.
 *
 * A sprite is positioned in nominal pixels on an 8x16 grid, and the playfield is
 * double-size rows -- 16x32 a cell. Those two grids coincide exactly at 2x2: a
 * 2x2 sprite at (16*col, 16*physrow) covers precisely the double-row cell at
 * (col, physrow). So a mover can be lifted out of the cell plane and keep the
 * position it already had, which is what makes sub-cell movement possible without
 * moving the dungeon underneath it. */
#define SPRITES     ((volatile unsigned char *)0xFE65)
#define SPR_STRIDE  6
/* Magnify, NOT size. Size (bits 4-2) composes adjacent glyph codes, so a 2x2 Winky
 * came out as glyphs $01 $02 $03 $04 tiled together -- four different characters in
 * one body. Magnify stretches the single pattern, which is exactly what a
 * double-size row does to a character, so a magnified sprite is the same picture
 * the cell plane was drawing before the movers were lifted out of it. */
#define SPR_MAG     0x20     /* bit 5 of each position high byte */
#define SPR_ENABLE  0x80

#define SPR_WINKY   0
#define SPR_ARROW   1
#define SPR_FACE    2
#define SPR_MON0    3                        /* MAX_MON slots */
#define SPR_HALL0   (SPR_MON0 + MAX_MON)     /* MAX_HALL slots */
#define SPR_COUNT   (SPR_HALL0 + MAX_HALL)   /* 15 of the chip's 17 */

/* Byte offset of each sprite's register block, so nothing has to work out slot*6 at
 * run time. The 6502 has no multiply, so that product was a call into cc65's mulax6
 * on every register access -- fifteen sprites, sixty times a second. The whole block
 * lives inside page $FE, so an offset is a byte and the base is a constant. */
#define SPR_OFFSETS { 0, 6, 12, 18, 24, 30, 36, 42, 48, 54, 60, 66, 72, 78, 84 }

/* Write one sprite's six registers, with no function call and no multiply.
 *
 * A macro rather than a function on purpose: the six values were arguments, and cc65
 * pushes every argument onto a software stack one byte at a time -- a quarter of the
 * machine went on pusha/pushax/ldaxysp with the actual stores lost in the noise. */
#define SPR_WRITE(off, nx, ny, ch, attr, mag)                                  \
    do {                                                                       \
        volatile unsigned char *r_ = SPRITES + (off);                          \
        const unsigned int nx_ = (nx), ny_ = (ny);                             \
        r_[0] = (unsigned char)nx_;                                            \
        r_[1] = (unsigned char)(((nx_ >> 8) & 0x03) | (mag));                  \
        r_[2] = (unsigned char)ny_;                                            \
        r_[3] = (unsigned char)(((ny_ >> 8) & 0x03) | (mag) | SPR_ENABLE);     \
        r_[4] = (ch);                                                          \
        r_[5] = (attr);                                                        \
    } while (0)

#define VCMD_FONTROM  8      /* render from the CP437 ROM */
#define VCMD_FONTRAM  9      /* render from font RAM (our glyphs) */
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
 * The one that comes into a room WALKS THROUGH THE WALLS, and it moves at very nearly
 * your own speed. It does not path round the layout, it does not get stuck on
 * anything, and nothing you can build between you and it helps: it comes straight at
 * you until you leave. That is the whole point of it, and it is why the room's second
 * doorway matters.
 *
 * Its speed is set the other way round from everything else here. The rest step every
 * Nth tick, which cannot express anything between "half your speed" and "all of it";
 * the intruder steps on every tick EXCEPT every Nth. So HALL_IN_SKIP is a fraction
 * (N-1)/N of Winky's speed -- 3 gives two thirds, 5 gives four fifths -- and larger is
 * faster. Fast enough that you cannot simply stroll away from it; slow enough that a
 * diagonal, which moves you on both axes at once while it only ever moves on one,
 * buys the time to reach a doorway. */
#define MAX_HALL      6
#define HALL_BASE     3      /* awake at level start; +1 per room looted */
#define G_HALLMON     0xE8   /* a hooded figure */
#define HALL_EVERY    4      /* hall: steps every Nth tick (slower than Winky) */
#define HALL_ROOM_TICKS 170  /* room: ticks of dawdling before one comes in */
#define HALL_IN_SKIP  2      /* room: the intruder steps on all but every Nth tick */

/* ---- tiles (what is in a cell, independent of what is drawn) ------------ */
#define T_FLOOR   0
#define T_WALL    1
#define T_TREAS   2
#define T_CORPSE  3   /* a killed monster; still lethal to touch */
#define T_EXIT    4   /* room: a doorway in the border -- walk out of it */
#define T_BLK0    5   /* hall: T_BLK0 + n is room n's outline */
#define T_VOID0   9   /* hall: T_VOID0 + n is inside it -- black, or filled if done */
#define T_DOOR0  13   /* hall: T_DOOR0 + n is an entrance to room n */

/* ---- glyphs -------------------------------------------------------------
 * The codes are CP437's, but most of the shapes are not: FONT_ART in venture.c
 * redefines them in the chip's font RAM at startup. Anything not listed there --
 * the solid wall block, the arrows, all the HUD text -- still renders from the ROM,
 * because font RAM is seeded from it and we overwrite only what we draw ourselves.
 *
 * Winky used to alternate between $01 and $02 every tick; at 16x32 that reads as a
 * flicker rather than as animation, so he does not. */
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
/* Once the treasure is yours the monsters are worth points, and the arcade tells you
 * so by recolouring them. Before that a kill scores nothing, which is invisible
 * otherwise -- you cannot see a rule you are being scored against. */
#define A_MON_WORTH 0x45 /* bright magenta */
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
 * Six since the playfield went to double-size rows. A step used to cover 8 px across
 * and 16 down; it now covers 16 and 32, so at the old rate of four everything crossed
 * the screen at twice the speed.
 *
 * This is the only lever Winky has -- he moves one tile per tick, so his speed IS the
 * tick rate. Slowing him therefore slows the clock everything else runs on, and the
 * divisors below were all pulled in by one to compensate: monsters, patrols and
 * intruders all come out within about a tenth of the speed they were at twelve ticks a
 * second, while Winky drops from twelve tiles a second to ten. */
#define TICK_RATE   6    /* 60/6 = 10 ticks per second */
#define MAX_CATCHUP 4    /* simulation steps per pass, so a stall cannot spiral */

/* Monsters step every Nth tick, which is how they end up slower than Winky without a
 * second clock. Four rather than three: a smaller room leaves less space to
 * out-manoeuvre anything, so the same relative speed reads as more pressure than it
 * used to. Hallmonsters patrolling the hall are slower again; the one that comes into
 * a room is in a different league entirely, see above.
 *
 * Nothing starts anywhere near a doorway: every layout is checked offline for at
 * least MIN_SPAWN_GAP tiles between each monster post and either arrival cell, so
 * walking into a room can never kill you before you have taken a step. */
/* How much of Winky's step the slide uses. A mover that slides for its WHOLE span
 * arrives at the new cell exactly as the next step is taken, so the picture runs a
 * full step behind the simulation and a direction press takes a beat to bite.
 * Arriving early buys most of that back.
 *
 * WINKY ONLY, and that is the point. He is the one you are steering, so latency is
 * what matters and a pause after arriving is worth paying. Nothing else is
 * input-driven: for a monster only continuity matters, and shortening ITS slide just
 * makes it lurch and stop -- a room monster steps every third tick, so a half-length
 * slide is nine jiffies of motion followed by nine of sitting still. Everything that
 * is not Winky slides across its whole cadence. Larger = snappier, and choppier. */
/* Fraction of Winky's step the slide uses. 1 means it fills the whole step.
 *
 * It was 2, meaning he crossed a cell in three frames of a six-frame tick and then
 * stood still for the other three -- a 50% duty cycle, which reads as choppy however
 * fast the motion is. That was an attempt to cut input lag, and it was aimed at the
 * wrong thing: the delay before a press is felt is the keystate being sampled once a
 * tick, and arriving early does nothing about that. If the lag wants fixing it wants
 * fixing where it is. */
#define SLIDE_DEN   1

/* Jiffies between redraws. Drawing every jiffy is what a sprite makes POSSIBLE, not
 * what the CPU can afford: fifteen sprites plus the slide arithmetic at 60 Hz ate
 * about four fifths of the machine, the tick accumulator could not keep up, and
 * step() started dropping ticks -- which showed up as monsters freezing mid-patrol,
 * because a dropped tick is a move that never happens. The simulation runs at ten
 * ticks a second, so three drawn positions per step is plenty. */
#define DRAW_EVERY  1

/* Motion classes. A mover's PICTURE is a pixel position advanced by a fixed step
 * each frame -- the way a C64 game moves a sprite -- rather than re-derived from its
 * grid cell every frame, which is what the first cut did and what made drawing cost
 * four fifths of the machine. The step for a class is worked out once when the tick
 * rate changes, so the frame loop is an add and no division at all. */
#define CL_FAST 0            /* Winky: one tick, and arrives inside it */
#define CL_TICK 1            /* the arrow and the room intruder: one tick */
#define CL_MON  2            /* room monsters: MON_EVERY ticks */
#define CL_HALL 3            /* Hallmonsters out in the hall: HALL_EVERY ticks */
#define CL_INTRUDE 4         /* the one that follows you in: HALL_IN_SKIP ticks */
#define CL_COUNT 5

/* Sub-pixel resolution of a mover's position: nominal pixels in 12.4 fixed point.
 * A nominal x reaches 639, so twelve integer bits are enough and four fractional
 * ones keep a step accurate to a sixteenth of a pixel. */
#define SUB 4

/* A room monster works a patch of floor rather than hunting you across the room.
 * MON_AGGRO is how close you have to come before it darts at you; MON_ORBIT is how
 * far it will drift from its post while nothing is happening. Aggro wants to be
 * short enough that crossing a room is a matter of picking a line between patches,
 * and long enough that a patch is genuinely dangerous to stand in. */
#define MON_AGGRO   5
#define MON_ORBIT   2

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
