# VENTURE — Design

A port of **Venture** (Exidy, 1981) for MFC (`VENTURE.PRG`, cc65). You are
**Winky**, a smiley-faced adventurer, raiding a dungeon for treasure while
invincible **Hallmonsters** close in behind you.

Not original work: this is a deliberate homage to a specific arcade game, and the
design below follows the original's rules rather than improving on them. Where MFC
forces a change, it is called out in [Fidelity](#fidelity) rather than quietly
absorbed.

## Why this machine suits it

- **Winky already exists in the hardware.** CP437 glyph `$01` is an outline smiley
  and `$02` is the filled one — the protagonist and a two-frame animation ship in
  the character ROM (`include/computer/Cp437Font.h`). Nothing to draw.
- **Venture is slow real-time.** It predates twitch play: you nudge, wait, commit.
  One cell per tick at 15 ticks/sec is faithful, and that is a quarter of what
  KERNEL PANIC already sustains.
- **It is the case the `$FE0F` control port was built for.** Eight-way movement
  while firing needs independent, key-up-aware bits; the keystroke FIFO cannot do
  it. Venture uses the port exactly as a joystick.
- **Two-tier structure suits a character display.** The arcade zoomed from dungeon
  map to room; we switch screens, which is cheaper and reads better at 80×25.
- **It fits.** ~2.5 KB of room data in a 30,464-byte `MAIN`.

## Structure

Three levels of four rooms each — twelve rooms — then it **loops**, faster and
more aggressive, forever. There is no boss and no ending: the run ends when the
last life goes. That is the original's shape, and it means "finished" is a state
this project can actually reach.

Two views:

**The hall.** An open arena with the four rooms drawn in it as hollow **outlines**,
each with **two entrances** notched into its wall. Winky crosses it; Hallmonsters patrol it. **Rooms stay blind** —
the hall shows an entrance, never the contents. You commit before you know what is
inside, which is where the dread lives.

The one thing the hall does tell you is which rooms are **done**: a looted room
seals both its entrances and fills in solid, so a hollow box becomes a solid one. That is the arcade's own signal, and it
only appears after you have already been in.

**Room interior.** Fills the screen. The treasure, the room's monsters, walls, and
**two doorways** — the one you came in by and one to run for.

## Movement

Adopted wholesale from KERNEL PANIC, which has already proved it on this hardware:

```
fixed-tick accumulator off jiffies()          (K_GET_JIFFIES, $FF39)
tickrate = jiffies per tick, default 4        → 15 ticks/sec
keystate() sampled ONCE per tick              ($FE0F)
one cell of movement per tick per held bit
```

Movement is exactly as smooth as the tick rate, direction bits are independent, and
there is no "most recent key wins" to fight — steering while firing is free.

**`tickrate` is the difficulty ramp.** Loop 1 runs at 4; each loop lowers it toward
`TICK_MIN`, so the whole world — Winky, monsters, Hallmonsters — speeds up together
without a single per-entity speed constant. KPANIC's overclock proves the pattern.

Facing is the last non-zero direction held, and it persists after you release, so
you can back away from something while still aiming at it.

## Controls

| Key | Bit | Action |
|---|---|---|
| Arrows | `$01`–`$08` | move (8-way; diagonals are both bits) |
| Space | `$10` | fire |
| Left Shift | `$20` | *(unused — reserved)* |
| `P` | — | pause |
| `Q` | — | quit to DOS |

Movement and fire come from the control port; `P` and `Q` come from the keystroke
FIFO, drained a few per pass exactly as KPANIC does it.

**ESC is deliberately not a quit key.** The host sends an arrow BOTH ways -- the
control-port bit and the ANSI sequence `ESC [ A` -- so a bare ESC arriving in the
FIFO is ambiguous by construction: every arrow starts with one. Treating it as quit
made any arrow key exit the game instantly. The three-byte sequence is swallowed
instead, since movement never needs decoding from the FIFO.

## Arrows

**One shot in flight**, fired in the facing direction, true to the original. You
cannot spray: a missed shot is a commitment, and the reload is the arrow clearing
the screen or hitting something. This is the single biggest source of tension in a
room, and it is also the cheapest possible projectile system — one x, y, dx, dy.

Cheap, but not free of ordering hazards. The arrow moves before the monsters do, so
the two can **swap cells** inside one tick — the arrow advances past a monster's cell,
then the monster steps into the arrow's — and unless something looks again the shot
passes straight through. `monsters_advance()` re-checks the arrow after every step for
exactly this reason. It only bit on ticks where monsters move, which made it look like
random bad luck rather than a bug.

## Hallmonsters

Invincible. Unkillable. They cannot be shot, blocked, or outrun indefinitely — they
are a **clock**, not an enemy. They patrol the hall and encroach on whatever room you
are lingering in; stay too long and one comes through a doorway.

**Their number grows as you loot.** The arcade's hall starts nearly empty and is
crawling by the fourth room — one screenshot has seven of them. One wakes per room
cleared, which is what stops the last room of a level being the easiest.

**The one that comes into a room walks through the walls.** It does not path round the
layout, it cannot be cornered, and nothing you put between you and it helps — it comes
straight at you until you leave. In the hall they walk it like anyone else; inside a
room they ignore it entirely. That asymmetry is the whole difference between a monster
and a deadline, and it is why the room's second doorway earns its keep.

This is what stops Venture being a leisurely looting exercise. Implementation is a
timer and a pursuit step, which is nearly free.

## Rooms, monsters and treasure

Six themed rooms, each with its treasure. A level deals four of them, rotating with
the level, so a full run of twelve room-visits is not four pictures seen three times
each.

Each room is one big open arena with a single wall structure in it — a hook, a
vault, a web of pillars, a pen, a switchback, a pinwheel — rather than a dense maze.
That came from the screenshots: the arcade's rooms are mostly open space, and the
structure is there to make you commit to a route rather than to make you thread a
corridor.

Two doorways apiece, on different sides. Either is a way out with the treasure. A
one-door room is a cul-de-sac you have to fight back out of, which is not how Venture
plays.

**The doors line up, both ways.** Come in the hall entrance on a room's east side and
you arrive at the room's east doorway; leave by its north doorway and you step back
into the hall at the north of its block. Which means the hall's entrances cannot be
part of `map_layout` — a slot holds a different room every level, so the entrances are
cut at runtime into the middle of whichever block edges match that room's doorway
sides. `exits_of()` reads a room's sides off its border; `cut_notches()` does the
cutting.

Getting this wrong is disorienting in a way that is hard to name and easy to feel: you
walk in from the right and are put down at the top, and the map you have built in your
head stops matching the building.

Glyphs came first and the names follow them. Rather than pick a codepoint that
ought to look like a necklace, the ROM was rendered and read, and whatever a shape
honestly resembles is what it became — so Venture's Necklace, Urn, Key, Crown and
Griffin are a Jewel, an Ingot, a Ring, an Amulet and a Wraith here.

| Room | Monster | Glyph | Reads as | Treasure | Glyph |
|---|---|---|---|---|---|
| Serpent | serpents | `§` `$15` | an S-curve | Apples | `♣` `$05` |
| Cyclops | cyclops | `Θ` `$E9` | one eye | Jewel | `♦` `$04` |
| Spider | spiders | `☼` `$0F` | radiating legs | Ring | `◘` `$09` |
| Goat | goats | `Ω` `$EA` | horns | Ingot | `■` `$FE` |
| Skeleton | skeletons | `¥` `$9D` | spine and ribs | Chest | `◙` `$0A` |
| Wraith | wraiths | `Φ` `$E8` | a hooded figure | Amulet | `♥` `$03` |

Picking these from a table without looking at them is how you end up with a spider
that reads as a snowflake.

Colour comes from the attribute plane, so each monster type and each treasure gets
its own without costing a glyph.

**The dungeon is recoloured every level** — magenta, then cyan, then yellow, with
the Hallmonsters on a colour of their own — exactly as the arcade does it. Two
tables of three bytes.

## Lethal corpses

A killed monster leaves a **body that still kills you**. Venture's signature
mechanic, and it changes what a room is: every kill shrinks the space you have to
move in, and clearing a room can wall you off from the treasure or the door.

Nearly free to implement — leave a glyph, collide, die — and it is the reason a
room cannot be brute-forced.

A body stops **monsters** too, and what they do about it matters more than it sounds.
They have to *route round* it. Vetoing the step after choosing it leaves the monster
standing still, which turns a room full of bodies into a room full of statues waiting
to be shot — so a corpse is tested as part of the pathing, with a deterministic
sidestep when both ways forward are shut.

They also remember one cell -- the one they came from -- and will not step straight
back into it unless there is nowhere else at all. Without that, a monster meeting a
long wall bounces between two cells forever: pursuit turns it at the wall, the wall
turns it aside, the next step turns it straight back, and it never reaches the end of
the wall. From the player's chair that is the stalling bug again wearing a different
hat, and it wants the same answer.

Monsters do not step onto each other either. They all chase the same target, so without
that they converge into one cell and draw as a single glyph: you cannot see how many are
coming, and one arrow appears to kill two.

## Scoring

| Event | Points |
|---|---|
| Treasure | 200 × level |
| Monster killed **after** taking the treasure | 100 × level |
| Monster killed **before** taking the treasure | **0** |

That last row is the whole risk system in one rule. The instinct is to clear the
room and then loot safely; the scoring inverts it — grab first, then decide whether
greed is worth staying in a room with a Hallmonster inbound and your own corpses in
the way. Preserve it exactly.

## Lives and progression

Three lives. Losing one costs the room, not the level — you are put back in the hall
and the room is still there. Losing the last ends the run. Clearing all four rooms of
a level descends. Clearing level 3 loops to level 1 with `tickrate` decremented.

Two screens frame a level, both from the arcade:

**The roster**, before you start: a slot for every treasure in the game, each a `?`
until you have taken that one, then its glyph in its own colour, over
`PLAYER 1 GET READY`. It is the only long-run progress the game shows.

**The tally**, between levels: `SCORE THIS LEVEL` × `BONUS MULTIPLIER` = `TOTAL
BONUS`. The screenshot shows a ×6 but not what sets it, so the rule here is ours —
the level you just finished plus the lives you still have, which reaches ×6 exactly
where the arcade's shot does and makes not dying worth something beyond not dying.

## Memory budget

`MAIN` is 30,464 bytes (`$0800`–`$7EFF`, with the 2 KB C stack above it at
`$7F00`–`$86FF` — note `__STACKSTART__` must be `$8700`, since `$8800` is DOS ROM).

| Item | Estimate |
|---|---|
| 12 room layouts, ~24×12 cells RLE | ~2.5 KB |
| Code | ~8–10 KB |
| Entity tables, HUD, scratch | ~1 KB |
| **Headroom** | **~17 KB** |

Comfortable. KERNEL PANIC is 8,299 bytes for a busier simulation.

## Status

The player-facing manual is `docs/VENTURE.md`. The game deliberately explains
nothing while it runs -- no tutorial messages, no coaching when you take the
treasure or kill something worthless. An arcade cabinet carried an instruction card
and the machine just played; the manual is that card.

**Finished.** All ten steps are in, then a fidelity pass against ten screenshots of
arcade play, then passes on what screenshots cannot show — how the things in the
rooms behave, and whether the geometry holds together when you actually walk it
(`VENTURE.PRG`, 12,152 bytes). Driven by `tests/test_venture.cpp` (26 tests), which
runs the real blob and holds keys through `$FE0F` the way a player would.

Things that came out of building it:

- **A missing repaint, found by the tests.** `step()` only restores cells belonging
  to things still alive, so when an arrow died its final cell was never redrawn --
  and that is exactly the cell a corpse gets written into. The body was in the grid,
  lethal and killing you, while the screen still showed an arrow or a serpent there.
  Both the wall-hit and monster-hit paths now repaint immediately.
- **Headless play needs the host timer.** The 60 Hz interval timer is pulsed by the
  GUI (`MainWindow`), not by `Computer6502::run()`, so a jiffy-paced program sits on
  its title screen forever in a test. The harness drives it off the CPU cycle
  counter (16,667 cycles per tick), which is a true 60 Hz rather than an
  instruction-count guess. Anything else jiffy-paced -- KERNEL PANIC included -- will
  need the same.
- **The map and a room are the same code.** One `grid[]`, one `restore()`, one
  `chase()`, one draw pass; they differ in dimensions, in what a door means, and in
  whether firing is allowed. That is why the hall cost almost nothing to add, and
  why a Hallmonster walking into a room needed no new machinery at all -- it is a
  chaser that ignores bodies and cannot be shot.
- **Greedy pursuit needs a wander fallback.** Closing the larger axis first leaves a
  chaser pressed against the hall's long wall bands with Winky straight through them
  and no second axis to try, standing still forever. A random open step when the
  chase produces no movement fixes it, and on the map it reads as the patrol a
  Hallmonster is supposed to be doing anyway.
- **A layout typo is invisible until someone plays it.** One wrong character seals a
  treasure off and the room is unwinnable, with nothing about the source looking
  wrong. `tests/test_venture.cpp` reads the pictures back out of `venture.c` and
  flood-fills them: exact dimensions, sealed border, treasure and every monster post
  reachable from the door. Two real typos turned up that way while the rooms were
  being drawn.
- **The tests moved with the geometry, twice.** Going to double-size rows changed every
  coordinate in `test_venture.cpp`: a logical tile is no longer a screen cell, so the
  glyph readers convert (logical row N is physical row `PLAY_ROW0 + 2N`) and everything
  above them kept working in tiles. What did NOT survive was the routes -- the hall is a
  different shape three times over now -- and those had to be rewalked by hand.
- **Three things resisted testing**, and are left untested rather than tested flakily.
  The Hallmonster that comes into a room you linger in needs Winky alive for 260 ticks
  with three serpents closing. The between-levels tally needs all four rooms of a level
  looted. And the arrow-swap fix has no signature on screen -- the arrow is drawn over
  the monster it is sitting on, so provoking the one observable frame needs a monster
  to step onto a live arrow, on a tick monsters move, while Winky is alive to watch;
  every setup tried passed on the broken build as often as the fixed one. A test that
  passes on the broken build is worse than no test. All three are written up in
  `test_venture.cpp` with what exactly they leave unverified.
- **Screenshots beat descriptions.** The build was "finished" before anyone looked at
  the arcade game running. Four mechanics that a written summary does not convey were
  sitting in plain sight: a looted room seals solid, Hallmonsters accumulate, rooms
  have two doors, and Winky carries a visible facing pip. All four were small changes
  and all four matter to how it plays. The pip in particular fixes a readability
  problem this port had invented for itself -- facing persists after you release the
  key, and nothing showed it.
- **Play it, or you will not find these.** The two bugs in this pass -- doors that did
  not line up, and shots that passed through monsters -- were both invisible from the
  code and from the tests, and both obvious within a minute of playing. The doors were
  matched by scan order, which is arbitrary and *looked* fine; the shots failed only on
  the one tick in three where monsters move.
- **A calibration bug hid behind three real ones.** The test harness had `TICK_RATE`
  hard-coded at 4, so every scripted leg silently shortened the moment the game's
  pacing was retuned -- and the failures it caused looked like gameplay problems. It is
  a constant now. Fixing it immediately surfaced a genuine one: monsters oscillating
  against a long wall, which the harness had never been accurate enough to catch.
- **A vetoed step is not a re-planned step.** Room monsters would not walk over a
  corpse, but the refusal happened *after* the greedy step had been chosen, so the
  monster simply did not move -- it stood next to its own dead until it was shot.
  Obstacles have to be part of the pathing, not a filter on its output. The fix also
  needed a deterministic sidestep for the case where both ways forward are shut, since
  falling back to a random step there reads as dithering.
- **Making the monsters better broke a test route.** The 160-tick loot route through
  room 0 survived only because the serpents used to stall; once they kept coming it
  died every time. The harness moved to the spider room, whose pillars leave two clear
  columns and make the round trip under 40 ticks. Worth noting that the *symptom* of
  the fix landing was a test failing.
- **Pursuers must not step into a doorway.** A room entrance is one cell let into a
  solid block, so a chaser that walks in has nothing to step to and rattles there for
  the rest of the level. `chase()` treats entrances as wall; they are patrolling the
  hall, not queueing to get in.
- **Aim and fire on the same tick.** `keystate()` is sampled once per tick and
  `winky_move()` sets facing before `fire()` reads it, so pressing a direction and
  fire together launches the arrow that tick. Turning first and firing second -- the
  obvious way -- walks Winky into whatever he is aiming at whenever it is close. The
  test harness had to learn this before it could reliably kill anything.
- **Counting ticks is not counting cells.** The game's fixed-tick accumulator and the
  harness's jiffy budget drift by a tick depending on phase, so a leg asked for in
  ticks lands one cell short often enough to matter -- and a route one cell short of
  its treasure walks the whole way back without it, looking exactly like a collision
  bug. Room legs now hold the key until the screen says Winky has moved that far.
- **The tests can catch a half-drawn frame.** `step()` erases every cell that can
  move and only then redraws them, and a cycle budget stops the CPU at an arbitrary
  instruction. Sampling in that window shows no Winky at all -- which looks exactly
  like a death. The finders retry over a few frames; a single-frame scan is flaky by
  construction, not by bad luck.

## Build order

Each step ends somewhere runnable and testable, the way KPANIC's did:

1. **Skeleton loop.** Fixed-tick accumulator, `keystate()` polling, Winky moving  **(DONE)**
   8-way in an empty bounded room, `Q` to quit. Proves pacing and input.
2. **One room, walls and treasure.** Collision, grab, exit. No monsters.  **(DONE)**
3. **Arrows.** One in flight, facing direction, wall collision.  **(DONE)**
4. **Room monsters.** One type (serpents), pursuit, death on contact, killable —  **(DONE)**
   plus lethal corpses.
5. **Scoring.** Including the before/after-treasure rule.  **(DONE)**
6. **Dungeon map view.** Four rooms, corridors, blind doorways, room entry/exit.
7. **Hallmonsters.** Map patrol plus room encroachment.
8. **All twelve rooms.** Remaining monster types and treasures.
9. **Levels, lives, loop.** Difficulty ramp via `tickrate`.
10. **Sound.** SID cues — Hallmonster approach is the one that matters.

Steps 1–5 are a playable single room; that is the point at which the design either
feels like Venture or does not, and it is worth stopping there to judge.

## Fidelity

Ten screenshots of arcade play were read late in the build, and most of what they
showed got implemented rather than excused: two doorways per room, looted rooms
sealing solid, the growing Hallmonster count, the per-level palette, the facing pip,
the roster and the tally, and open-arena room shapes instead of mazes.

What is left is a genuine departure, and why:

- **Cell-granular movement.** The arcade had sub-cell motion; character cells *are*
  the resolution here, with no sprite layer. At 15 cells/sec with independent
  direction bits this reads as smooth arcade movement, not as a board game — but it
  is a real difference and the one thing no amount of engineering removes.
- **Screen switch instead of a zoom** between hall and room. The arcade draws each
  room's true shape on the hall and zooms into it; a 44×15 room cannot also be drawn
  to scale inside a 56×15 hall, so the hall carries an 11×5 box per room instead. It
  is drawn hollow, with its entrances notched into it on the sides that match the
  room's own doorways, and fills in solid when looted.
- **An entrance sits at the middle of its block edge**, wherever the room's doorway is
  along its wall. On an eleven-cell edge there is nowhere meaningfully different to put
  it; the *side* is what a player reads.
- **No moving walls.** `room-moving-walls.png` has red bars that slide; that is a
  per-room hazard system and it is not built. The pinwheel room takes its shape from
  that screenshot without its motion.
- **Six room layouts, not twelve.** Dealt four at a time and rotated by level.
- **Six treasures on the roster, not twenty-seven.** It lists what this port has.
- **Glyphs, not sprites.** The char-cell look is the aesthetic, as with KPANIC.
- **Sound is suggestive, not sampled.** Cues on one SID voice, not the arcade's board.

Not departures, and not to be "improved": the invincible Hallmonsters, the lethal
corpses, the zero-points-before-treasure rule, one arrow in flight, blind rooms, and
the absence of an ending. Each is load-bearing.
