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
extern void          vfseek(unsigned int index);      /* soft-font byte index */
extern unsigned char vfread(void);                    /* ...read, auto-increments */
extern void          vfwrite(unsigned char b);        /* ...write, auto-increments */
extern unsigned int  rng_seed(void);                  /* RTC-derived RNG entropy */
extern unsigned char rtc_sec(void);                   /* BCD seconds; tested for change */
extern unsigned int  jiffies(void);                   /* 60 Hz monotonic tick counter */
extern unsigned char keystate(void);                  /* live held-key bitmask ($FE0F) */
/* Sprites. Pixel-positioned and, crucially, NOT riding the scroll region -- the only
 * place a screen-fixed object can live once the world scrolls in sub-cell steps.
 * Select a sprite, then set its fields. spr_x/spr_y take CELLS and the glue converts;
 * spr_x_px/spr_y_px take the chip's nominal pixels directly, for an object that sits
 * BETWEEN cells. The craft takes pixel X (it steers sub-column) and cell Y (its row never
 * moves); the shots are the mirror image -- cell X, pixel Y as they slide between rows.
 *
 * Sprite 0 is the craft; 1..MAX_SHOTS are the shots. Both were juddering in the cell
 * plane for the same reason, and both are cured the same way. */
extern void          spr_sel(unsigned char index);
extern void          spr_x(unsigned char cell_col);
extern void          spr_y(unsigned char cell_row);
extern void          spr_x_px(unsigned int pixel_col);
extern void          spr_y_px(unsigned int pixel_row);
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
#define KS_BOOST    0x20        /* Left Shift. Unused by this game -- kept because it
                                 * documents the control port's bit 5, which exists
                                 * whether or not a program reads it. */

/* ---- VIC command codes ---- */
#define VCMD_CLEAR      0x01
#define VCMD_SCROLLUP   0x02
#define VCMD_SCROLLDOWN 0x03    /* content moves down; row 0 opens (filled) */
#define VCMD_FILLROW    0x04
#define VCMD_FINEY      0x0C    /* param = pixels to slide the scroll region down */
#define VCMD_FONTROM    0x08    /* render from the CP437 ROM (the default) */
#define VCMD_FONTRAM    0x09    /* render from font RAM */
#define VCMD_FONTRESET  0x0A    /* reload CP437 into every font set */
#define VCMD_FONTSET    0x0B    /* param = which set is live */

/* ---- the soft font, used for exactly one thing ----
 * A fragment is drawn as a two-cell CHIP: a framed component with the weapon's letter
 * centred across the cell boundary. It was a bare letter, then a letter on a dim navy
 * tile, and neither read as an OBJECT -- they read as text that happened to be in the
 * playfield, which is the wrong signal for the one thing you chase.
 *
 * The six glyphs are BUILT AT RUNTIME rather than shipped as artwork: font RAM is
 * readable and seeded with CP437, so the program lifts the real 'S'/'B'/'H' shapes out
 * of it, shifts each four pixels right so the letter straddles both cells, and ORs a
 * frame around it. No hand-drawn letters to keep in sync with the font, and the letter
 * is exactly the one the rest of the game uses.
 *
 * Codes 16-21 are chosen because nothing else in this game draws them, and font RAM is
 * CP437-seeded so all 250 other glyphs are untouched. The font is put back to ROM on the
 * way out -- see main() -- so DOS never inherits them. */
#define FONT_SET        0       /* set 0: CP437 everywhere except our six codes */
#define GL_FRAG_FIRST   16      /* 16/17 = S, 18/19 = B, 20/21 = H (left/right) */
#define FRAG_ROWS       16      /* scanlines per glyph */

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
 * the enemies and the pellets all ride it correctly -- but anything screen-referenced
 * sawtooths by one cell per row. That is why the craft and the shots are sprites: a
 * sprite does not ride the region, and it is pixel-positioned so it can sit between
 * cells in both axes. */
#define CELL_H          16      /* pixel height of an ordinary row */

/* World speed is PIXELS PER FRAME, accumulated: each frame adds it to a sub-cell
 * offset, and when the offset passes a cell height the chip scrolls a row and the
 * simulation takes its step.
 *
 * This replaced "frames per step", which had run out of room -- it was already at 1,
 * its floor, so there was no way to move it. An accumulator also frees the step from
 * having to divide 16, so speed is a smooth dial (2, 3, 4, 5...) rather than the three
 * usable values a divisor gave.
 *
 * The dial belongs to the PLAYER and nothing else may write it. It is a throttle, in the
 * River Raid sense: faster covers more rows for the same energy (drain is per row) and
 * so scores faster, at the cost of reaction time. That trade is the reason to touch it. */
#define SPEED_MIN       1       /* 3.75 rows/sec */
#define SPEED_MAX       8       /* 30 rows/sec */
#define SPEED_DEFAULT   2       /* 7.5 rows/sec -- where a run starts */

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

/* Most islands are short pillars to weave around. Now and then the generator builds a
 * BIG one instead: wide and long enough to split the conduit into two committed lanes.
 * It is the one terrain feature that makes you pick a side and live with it for several
 * seconds, and the original's big mid-river land masses are exactly that -- so this is
 * variety with a purpose rather than variety for its own sake.
 *
 * A big one needs a wider channel than a pillar does, because it has to leave a real
 * lane either side rather than a gap. */
#define ISL_BIG_HW    15        /* big islands need at least this much half-width */
#define ISL_BIG_ONE_IN 2        /* 1-in-N islands go big, where the channel allows.
                                 * 2 lands one big island per ~330 rows -- a bit over
                                 * one a sector, so it is a set piece you remember
                                 * rather than the shape of the terrain. */
#define ISL_BIG_W     5         /* big island: W + rnd(4) columns wide */
#define ISL_BIG_ROWS  10        /* big island: ROWS + rnd(8) rows long */

/* ---- timing ----
 * TICK_* are jiffies-per-simulation-step at 60 Hz. Difficulty scales by shrinking
 * this divisor, never by fractional speeds (cc65 has no float).
 *
 * This paces the VISUAL FRAME, not a step: one jiffy per frame = 60 frames/sec, and
 * how far the world moves in each is SPEED_* above. It is left adjustable only because
 * a slower frame rate is occasionally useful for watching a bug. */
#define TICK_DEFAULT 1
#define TICK_MIN     1
#define TICK_MAX     6
#define MAX_CATCHUP  8          /* visual steps per pass before we resync to now.
                                 * A whole row's worth, so a slow frame can still
                                 * complete the row it is partway through. */

/* Steering reads the PIA's live key-state port, which replaced a pile of heuristics that
 * tried to infer "key still held" from the keystroke stream -- impossible, because that
 * stream has no key-up and the host only auto-repeats the most recent key, which is why
 * you could never move and fire at the same time.
 *
 * It is sampled and applied once per FRAME, and the craft's position is carried in PIXELS
 * rather than columns. Both of those are deliberate:
 *
 *  - Per frame, not per world step. Steering used to live in step_world(), which the
 *    fine-scroll accumulator invokes once per ROW -- so lateral speed was welded to
 *    scroll speed at 7.5 columns/sec, and the craft was at its least manoeuvrable in the
 *    slowest sector, the one meant to be teaching. It is now independent of the throttle.
 *  - In pixels, because the craft is a hardware sprite and a sprite is pixel-positioned.
 *    Moving it a whole cell at a time threw away resolution the chip already had. Same
 *    trick draw_shots() uses for Y: draw at pixel precision, resolve collision on the
 *    cell grid.
 *
 * Columns per second = 60 * STEER_PX / CELL_W. */
#define CELL_W          8       /* pixel width of a cell */
#define STEER_PX        3       /* pixels per frame held -> 22.5 columns/sec */

/* ---- the shared ENERGY pool (the signature mechanic; see DESIGN.md) ----
 * One number is simultaneously your fuel, your clock and your distance budget: it
 * drains as you travel, and refills only from data nodes.
 *
 * ENERGY_DRAIN is charged once per ROW, not once per jiffy -- energy_spend() lives in
 * step_world(), which the fine-scroll accumulator invokes exactly once per cell of
 * travel. So energy is a DISTANCE budget and speed does not enter into it: a full tank
 * is ENERGY_MAX/ENERGY_DRAIN = 500 rows however fast you cover them.
 *
 * The prices below were retuned once it was clear the merge of "damage" into "fuel"
 * had not been costed. River Raid keeps them apart -- a crash there takes a LIFE, not
 * fuel -- and we deliberately fused them, but at the old prices a crash cost 150
 * against a baseline of 2/row, so ONE crash ate the entire surplus of one node taken.
 * A player claiming 70% of nodes and crashing once per 150 rows netted -41 per 150
 * rows: a competent player bled to death by arithmetic. Mistakes are the dominant
 * consumer of this pool, so they are now priced as setbacks rather than as thirds of
 * a tank, and nodes are considerably less scarce -- see sector_node[]. */
#define ENERGY_MAX      1000
#define ENERGY_DRAIN    2       /* per ROW travelled, unconditionally */
#define ENERGY_NODE     350     /* refill for taking a node instead of shooting it */
#define ENERGY_CRASH    70      /* cost of hitting the conduit */
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
#define W_BEAM           2      /* B: a tall fast bolt that punches through */
#define W_HOMING         3      /* H: shots drift toward the nearest target */
#define W_MAXLEVEL       3

/* A special weapon is a MAGAZINE, not a permanent upgrade. Picking one up used to be
 * forever: collect homing once and you never fired anything else for the rest of the
 * run, which flattened the whole pickup chain into a single decision made in the first
 * thirty seconds. A magazine makes a fragment a resource to spend and re-earn.
 *
 * One trigger pull spends ONE round whatever the volley's shape -- a five-shot spread
 * and a four-cell beam bolt each cost the same as a single plain shot. Counting
 * projectiles instead would make spread burn five times faster than beam for the same
 * press, which is a tax on the weapon that is supposed to be wide.
 *
 * Collecting the same kind again refills AND deepens; a different kind swaps you to it
 * at level 1 with a full magazine. Empty reverts to W_PLAIN, which is unlimited. */
#define W_AMMO          10      /* rounds a fragment grants */
#define W_AMMO_LOW       3      /* at or below this the HUD count turns red */

/* One sprite per shot plus one for the craft, and the chip has 17 -- so 16. In play
 * that is generous: the plain gun keeps about 3 in the air, spread level 2 about 9.
 *
 * A full pool must never change the weapon's SHAPE, though, and this is the trap: fire()
 * spawns the centre shot first and works outwards, so dropping individual shots eats the
 * WINGS and level 3 spread quietly renders as level 1 at exactly the moment you earned
 * it. So a volley is ALL OR NOTHING -- if the whole thing does not fit, nothing fires
 * and the shot is retried next tick. Saturation costs you rate, which is legible,
 * instead of silently narrowing the gun. */
#define MAX_SHOTS        16
#define SHOT_SPEED       2      /* screen rows per tick, substepped so it can't tunnel */
#define FIRE_COOLDOWN    3      /* ticks between shots */

/* ---- the beam ----
 * The beam used to be a plain shot that simply didn't die on impact -- mechanically
 * distinct, visually identical, so it read as a bug rather than a weapon. It is now a
 * BOLT: a stack of BEAM_CELLS cells in one column, travelling together and fast.
 *
 * The stack is ordinary pool shots, not a new object kind. Four cells of one column
 * moving at one speed ARE a tall bolt, and the pool already enforces all-or-nothing
 * volleys, so nothing new was needed -- a partial bolt is refused the same way a partial
 * spread is. Each cell still pierces, which costs nothing in practice: whatever the
 * leading cell kills is gone before the next arrives, so the tail only matters if
 * something moves into the column behind it. Against a wall the cells arrive one after
 * another and the bolt visibly crumples into it instead of vanishing.
 *
 * BEAM_CELLS * bolts-in-flight must stay inside MAX_SHOTS. At BEAM_SPEED a bolt clears
 * the band in ~6 steps against FIRE_COOLDOWN's 3, so two are typically up and the pool's
 * four-bolt ceiling is never the thing you notice. */
#define BEAM_CELLS       4      /* rows tall */
#define BEAM_SPEED       4      /* rows per tick -- twice a plain shot */

/* ---- spread is the CLOSE-RANGE gun ----
 * Spread shots burn out at SPREAD_FLOOR instead of running to the top of the band. This
 * started as a pool problem and turned into the weapon's identity.
 *
 * The problem: one sprite per shot, 16 for shots after the craft takes one, and Lv3 puts
 * five shots up every FIRE_COOLDOWN steps with an 11-step flight. That is 5 * 11/3 = 18
 * in flight against a pool of 16, so the volley check refused about one press in five.
 * Not lost shots -- a refused volley retries next tick -- so the gun already self-paced
 * to roughly cooldown 3.7. The defect was that the limit was IRREGULAR (it depended on
 * when earlier shots happened to expire, so the gun stuttered) and invisible to both the
 * player and the next person to read MAX_SHOTS' claim that 16 was "generous".
 *
 * A fixed cooldown of 4 was measured and rejected: 150 volleys against the broken
 * version's 164, so it fired LESS while looking like a fix. Enlarging the chip's sprite
 * block would have worked (20 is the threshold; 19 buys nothing) but spends half the
 * remaining I/O page on one weapon's peak.
 *
 * Short range fits the pool BY CONSTRUCTION and buys a real trade-off instead of a
 * compromise: spread clears crowds close in, the beam reaches, homing tracks. Applied at
 * every spread level, because it is what the weapon IS rather than a penalty on Lv3.
 * Measured at floor 6: peak 15 of 16, 200 volleys, nothing refused. */
#define SPREAD_FLOOR     6      /* spread shots expire at this row (0 = top of band) */

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

/* Kill/impact debris. Without these a hit had no signature at all -- the target simply
 * stopped being drawn, which reads as the shot having passed through it.
 *
 * One hit seeds a SCATTER of cells rather than a single marker. The 2600 original draws
 * its explosions as a loose spray of dots spread over an area, and that reads far better
 * than a symbol sitting in one cell: a spray says the thing came apart, a symbol just
 * says something is here. Each cell fades through three glyph/colour stages as it ages,
 * and the outer cells are seeded already part-faded, so the spray collapses inward
 * instead of every cell blinking off together. */
#define POP_CELLS       6       /* debris cells one hit throws */
#define POP_TICKS       3       /* life of the innermost cell, in ticks */
#define MAX_DEBRIS      24      /* pool: 4 concurrent hits, and a firewall throws 3 */

#define E_NONE          0
#define E_DAEMON        1       /* closes on you faster than the world scrolls */
#define E_WORM          2       /* rides the world, weaving across the channel */
#define E_SENTINEL      3       /* rides the world, fires aimed pellets */

#define SPAWN_MIN       18      /* ROWS between spawns: MIN + rnd(VAR) */
#define SPAWN_VAR       22
#define SPAWN_FLOOR     6       /* tightest interval past the sector table. Below this,
                                 * MAX_ENEMIES saturates and spawn_enemy() starts
                                 * returning early -- difficulty would stop rising while
                                 * appearing to, which is the worst kind of dial. */
#define SENTINEL_FIRE   14      /* ticks between a sentinel's shots */

/* Everything dies to one shot, as in the original. Tiered health was the single
 * biggest thing making the game feel unresponsive: a 3-shot sentinel closing on you
 * needs three hits landed inside the time it takes to arrive, and when the third does
 * not land in time the two that did are indistinguishable from shots that passed
 * straight through. Targets are differentiated by BEHAVIOUR and by what they are worth,
 * not by how much punishment they soak -- so a sentinel is dangerous because it shoots
 * back and gets faster every sector, not because it is a tank. */
#define HP_DAEMON       1
#define HP_WORM         1
#define HP_SENTINEL     1

#define ENERGY_HIT      60      /* flying into corruption */
#define ENERGY_PELLET   30      /* taking a pellet */

/* ---- sectors ----
 * Same engine, new name and tempo. The design calls this the cheapest way to make
 * progress feel like progress, and it is: the only per-sector state is a speed, a
 * spawn rate and a label. */
#define NSECTORS        4
#define SECTOR_ROWS     240     /* rows of conduit before the next sector */

/* A sector escalates on FOUR axes, not just tempo -- because tempo alone turned out not
 * to escalate the thing it appeared to. Nodes are placed once every N ROWS, so a faster
 * sector delivers them faster in TIME as well, and the share of nodes you must take to
 * stay alive works out to DRAIN * rows / ENERGY_NODE, which has no tempo term in it at
 * all: a flat 51% in every sector. The world got harder to dodge and no harder to fuel.
 *
 * So node scarcity is its own dial, and the sentinels' rate of fire is another -- an axis
 * that is not speed. Sector 1 also gets a floor under the channel width and no gauntlets,
 * because a conduit that squeezes shut in the first ten rows teaches nothing. */
#define HW_MIN_EASY     7       /* sector 1's half-width floor, against HW_MIN's 4 */

/* ---- the firewall ----
 * River Raid's bridge: a barrier across the conduit with a single PORT in it. Shoot the
 * port and the whole thing goes; fly into it and you pay for it.
 *
 * It is TERRAIN, not an object -- one flagged row in the same ring the walls live in. So
 * it rides the hardware scroll for free, needs no erase/redraw of its own, and blocked()
 * already stops the craft on it. That is the entire implementation, and it is why this
 * replaced a phased boss that had to stop the world to avoid sawtoothing against the
 * fine offset: there is nothing here that is not already solved.
 *
 * There is no gate and no checkpoint. The game is how far you can go, so a firewall is a
 * toll rather than a door: open it for score, or take the hit and carry on. */
/* Firewalls are paced on their OWN counter, not on the sector boundary. Tying them to
 * sectors put the first one at row 240, and since it is generated at the top of the band
 * it is not MET until row ~264 -- so a run that ended earlier never saw the signature
 * obstacle even once, which is exactly what happened.
 *
 * 80 rows is the original's cadence rather than a guess. A River Raid section is
 * SECTION_BLOCKS(16) * BLOCK_SIZE(32) = 512 scanlines and its main display is 160, so a
 * bridge arrives every ~3.2 screens. Our band is PLAY_H rows, and 3.2 * 24 = 77. */
#define FW_ROWS         80      /* rows of conduit between firewalls */
/* The port is TWO cells and dies to ONE shot.
 *
 * It was one cell and three shots, and both halves were wrong for reasons already
 * written down elsewhere in this file. NODE_W is 2 because "one cell was too fine a
 * target to line up on while dodging" -- the port is the same problem with a hard
 * deadline attached. And every enemy became one-shot; a barrier needing three made the
 * only mandatory target in the game the toughest thing in it.
 *
 * Note this was NOT why the port seemed to ignore damage -- that was shot tunnelling,
 * see shots_advance(). Fixing the feel and fixing the bug were separate jobs. */
#define FW_PORT_W       2
#define FW_PORT_HP      1       /* shots to blow the port, and with it the barrier */
#define FW_SCORE        150
#define FW_CRASH        120     /* energy for flying into one -- worse than a wall */

/* ---- scoring ----
 * unsigned int caps at 65535; step 7 widens this to two words if bosses and long
 * runs start pushing it. */
#define SCORE_NODE      25
#define SCORE_DAEMON    15
#define SCORE_WORM      25
#define SCORE_SENTINEL  40

/* ---- fragments (weapon pickups) ---- */
#define MAX_FRAGS       3
/* 1-in-N chance a kill drops one. Briefly 12, on the theory that one-shot kills had
 * doubled the kill rate so the drop rate should halve. That was reasoning about the wrong
 * quantity: what the player experiences is the drop CADENCE, and at 12 it works out to
 * one every ~29 seconds in sector 1 against runs that often last 60-90. Power-ups stopped
 * appearing at all. 6 puts it at ~14 s in sector 1, tightening to ~6 s by sector 4 as the
 * spawn interval closes -- often enough to be part of the game. */
#define FRAG_CHANCE     6
/* Two cells wide, and for the reason already written down for NODE_W: "one cell was too
 * fine a target to line up on while dodging". A fragment is the one pickup you actively
 * chase, so it is the worst thing in the game to make hard to line up on. Two cells is
 * also exactly an enemy's footprint, so a fragment covers the body that dropped it. */
#define FRAG_W          2

/* ---- attributes: [R][BR][bg:3][fg:3]; 0x40 = bright ---- */
#define A_WALL      0x46        /* bright cyan -- conduit wall (the lethal edge) */
#define A_BEVEL     0x06        /* cyan -- the wall's outer bevel cell */
/* The board outside the conduit is the SECTOR's colour -- the peripheral cue that you
 * have crossed into a new zone. The banner names the sector, but a banner scrolls past
 * in three seconds and then the screen looks identical to the one before it.
 *
 * All four are non-bright, because the board is background: thin traces that must not
 * compete with anything you have to react to. That rules out most of the palette --
 * 0x01 red is the firewall barrier, 0x06 cyan is the wall's bevel and would sit directly
 * against the board and destroy the shaped-lip reading, and 0x04 blue renders near-black
 * here (the same trap A_FRAG fell into). Green, amber, magenta and grey are what is left,
 * and grey last suits I/O.
 *
 * The channel's recessed board (A_RECESS) deliberately does NOT change: the lane you fly
 * in should read the same everywhere, or every sector becomes a re-learn. */
#define A_BOARD     0x02        /* green  -- KERNEL, and the default */
#define A_BOARD2    0x03        /* amber  -- HEAP */
#define A_BOARD3    0x05        /* magenta-- STACK */
#define A_BOARD4    0x07        /* grey   -- I/O */
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
#define A_FIRE      0x01        /* DIM red -- the firewall barrier. Deliberately not
                                 * bright: it does not move and it is not the thing you
                                 * aim at, so it must not compete with the port. */
#define A_PORT      0xC7        /* REVERSED bright white -- the port. Inverted so it
                                 * reads as a target rather than more barrier. */
#define A_SPENT     0x07        /* dim white -- a spread shot in its last rows before it
                                 * burns out. Without this the shots simply vanish in mid
                                 * air, which reads as a rendering fault rather than as
                                 * the weapon running out of reach. */
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
#define G_BEAM      186         /* double vertical -- one cell of a beam bolt. A bar,
                                 * not an arrow: stacked bars join into a continuous
                                 * column, where stacked arrows would read as four
                                 * separate shots flying in formation. */
#define G_DAEMON    31          /* solid down triangle -- coming at you */
#define G_WORM      215         /* weaving corruption */
#define G_SENTINEL  4           /* diamond -- emplaced, shoots */
#define G_PELLET    7           /* enemy shot */
#define G_BAR_FULL  219         /* energy bar: filled cell */
#define G_BAR_EMPTY 176         /* energy bar: empty cell */
#define G_CRAFT     30          /* solid up triangle */
/* Debris fades BLAST -> EMBER -> DUST as a cell ages. Three glyphs of decreasing
 * density, so the spray visibly thins rather than switching colour in place. */
#define G_BLAST     15          /* sun -- the dense heart of the burst */
#define G_EMBER     249         /* small bullet -- a cooling fragment */
#define G_DUST      250         /* middle dot -- the last of it */
#define G_FIRE      177         /* dark shade -- the barrier itself */
#define G_PORT      254         /* small solid square -- the port to shoot */
#define G_HBAR      196         /* single horizontal -- HUD rule */

/* ---- kpanic.c ---- */
unsigned int  rnd16(void);
unsigned char rndn(unsigned char n);
signed char   utoa(unsigned int v, char *buf);   /* -> reversed digits; count */

#endif /* KPANIC_H */
