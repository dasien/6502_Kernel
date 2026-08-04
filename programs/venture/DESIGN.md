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

**Dungeon map.** The level's four rooms as boxes joined by corridors, Winky moving
between them, Hallmonsters patrolling. **Rooms stay blind** — the map shows a
doorway, never the contents. You commit before you know what is inside, which is
where the dread lives.

**Room interior.** Fills the screen. The treasure, the room's monsters, walls, and
the door you came in by.

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
| `Q` / ESC | — | quit to DOS |

Movement and fire come from the control port; `P` and `Q` come from the keystroke
FIFO, drained a few per pass exactly as KPANIC does it.

## Arrows

**One shot in flight**, fired in the facing direction, true to the original. You
cannot spray: a missed shot is a commitment, and the reload is the arrow clearing
the screen or hitting something. This is the single biggest source of tension in a
room, and it is also the cheapest possible projectile system — one x, y, dx, dy.

## Hallmonsters

Invincible. Unkillable. They cannot be shot, blocked, or outrun indefinitely — they
are a **clock**, not an enemy. They patrol the map and encroach on whatever room you
are lingering in; stay too long and one comes through the door.

This is what stops Venture being a leisurely looting exercise. Implementation is a
timer and a pursuit step, which is nearly free.

## Rooms, monsters and treasure

Six themed rooms drawn from across the three levels, each with its treasure.

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

Colour comes from the attribute plane, so each monster type gets its own without
costing a glyph.

## Lethal corpses

A killed monster leaves a **body that still kills you**. Venture's signature
mechanic, and it changes what a room is: every kill shrinks the space you have to
move in, and clearing a room can wall you off from the treasure or the door.

Nearly free to implement — leave a glyph, collide, die — and it is the reason a
room cannot be brute-forced.

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

Three lives. Losing one restarts the current room; losing the last ends the run.
Clearing all four rooms of a level descends. Clearing level 3 loops to level 1 with
`tickrate` decremented and monster counts raised.

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

**Steps 1-5 are done** -- one playable room (`VENTURE.PRG`, 5,628 bytes). Serpents
hunt, one arrow flies at a time, bodies stay lethal, and the scoring rule holds.
Driven by `tests/test_venture.cpp` (8 assertions), which runs the real blob and
holds keys through `$FE0F` the way a player would.

Two things that came out of building it:

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

Deliberate departures, and why:

- **Cell-granular movement.** The arcade had sub-cell motion; character cells *are*
  the resolution here, with no sprite layer. At 15 cells/sec with independent
  direction bits this reads as smooth arcade movement, not as a board game — but it
  is a genuine difference and the one thing no amount of engineering removes.
- **Screen switch instead of a zoom** between map and room.
- **Glyphs, not sprites.** The char-cell look is the aesthetic, as with KPANIC.
- **Sound is suggestive, not sampled.** Three SID voices, not the arcade's board.

Not departures, and not to be "improved": the invincible Hallmonsters, the lethal
corpses, the zero-points-before-treasure rule, one arrow in flight, and the absence
of an ending. Each is load-bearing.
