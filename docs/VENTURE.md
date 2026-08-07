# VENTURE — Player's Manual

A port of **Venture** (Exidy, 1981). You are **Winky**, a smiley-faced adventurer
raiding a dungeon for treasure while things that cannot be killed close in behind
you.

The game explains nothing while you play — an arcade cabinet had an instruction
card bolted to it and the machine itself just played. This is the card.

## Starting

```
]OPEN GAMES
GAMES]VENTURE
```

Press any key at the title, at the treasure roster, and at each **CAUGHT** or
between-levels tally.

## Controls

| Key | Action |
|---|---|
| **↑ ↓ ← →** | Move. Hold two together for a diagonal. |
| **Space** | Fire an arrow |
| **P** | Pause |
| **Q** | Quit to the DOS |

**Hold the keys — don't tap them.** The machine reads which keys are down *right
now*, with no repeat delay, so holding a direction gives smooth movement and
tapping gives you one cell. It also means you can move and fire at the same time,
and steer diagonally, which a keyboard alone could not manage.

ESC does nothing on purpose. Every arrow key sends an ESC as the first byte of its
sequence, so a key that quit on ESC would quit whenever you moved.

## Before a level

A roster of every treasure in the game, each slot a `?` until you have taken that
one, then its own glyph in its own colour. It is the only long-run progress the game
shows, and the reason to go back into a room type you have already survived.

## The hall

The game proper opens on the **dungeon hall** — an open arena with the level's four
rooms sitting in it as blocks.

| | |
|---|---|
| `☺` | **Winky** — you |
| a hollow box | A room. **Two entrances apiece**, on different sides. |
| `■` | An entrance. Walk onto it to go in. |

| a solid box | A room you have looted: entrances sealed, filled right in. |
| `Φ` | **Hallmonster** — see below |
| `█` (dim) | Wall |

**You cannot see into a room from the hall, ever.** The entrance tells you nothing
about what is behind it until you have been inside. That is deliberate: you commit
first and find out second. The one thing the hall does tell you is which rooms are
*done* — and only after you have already been in them.

**The doors line up.** An entrance on a room's east side opens on the room's east
doorway, and leaving by the room's north doorway puts you back at the north of its
block. You can navigate the dungeon by memory rather than by surprise.

**You cannot fire in the hall.** The bow is for rooms.

Clear all four rooms and you descend a level, and the whole dungeon changes colour:
magenta, then cyan, then yellow. Three levels, and then it starts again — faster.
There is no ending; you play until your lives run out.

## Hallmonsters

`Φ` They patrol the hall. They cannot be killed, shot, blocked or reasoned with —
an arrow simply stops on one. They kill you on contact, and they are always walking
toward you.

They are slower than you are, so the hall is survivable if you keep moving. What
they are really there for is the clock: **linger too long in a room and one comes
through a doorway after you.** You will hear it arrive.

**Inside a room it walks through the walls.** It does not go round anything, it cannot
be cornered, and nothing you put between you and it helps. There is one answer and it
is to leave — by whichever doorway is not on its side of the room.

**There are more of them as you go.** One at the start of a level, and another wakes
every time you loot a room — so the fourth room of a level is not the easy one.

## Inside a room

| | |
|---|---|
| `☺` | **Winky** — you |
| `§ Θ ☼ Ω ¥ Φ` | The room's monsters — one arrow each |
| `♣ ♦ ◘ ■ ◙ ♥` | Its treasure — apples, jewel, ring, ingot, chest, amulet |
| `░` | **Remains** — what is left of something you killed |
| `█` | Wall |
| `↑ ↓ ← →` | Your arrow, in flight |
| `≡` | A doorway. There are **two**, and either is a way out. |
| `▲ ▼ ◄ ►` | A dim pip showing where your next arrow will go |

The bar across the top shows **SCORE**, **LIVES** and **LEVEL**.

Take the treasure, then get out. Every room has two doorways on different sides — the
one you came in by and one to run for — and neither will let you leave empty-handed.
Which one you leave by decides where in the hall you come out.

**Watch the pip.** Facing sticks after you let go of the key, and the pip is the only
thing that shows it. It is how you know what you are aimed at while you are backing
away from something.

**One arrow at a time.** You cannot spray. The shot flies in whatever direction you
last moved, and until it hits something or reaches a wall you are unarmed. Facing
sticks after you let go of the key, so you can back away from a serpent while still
aiming at it — often the only way to fight something that is chasing you.

**The dead do not clear away.** A killed monster leaves remains, and remains kill
you exactly as dead as the monster did. Every kill makes the room smaller. Clear a
room carelessly and you can wall yourself off from the treasure, or from the door.

The remains stop monsters too — but they **go round**. Killing something does not buy
you a wall, only a hazard you both have to work around. Monsters will not walk through
each other either, so a narrow gap is worth holding.

**Scoring is the trap.** The treasure is worth 200 × level. A monster is worth
100 × level — *but only after the treasure is in your hand*. Kill anything before
that and it is worth nothing at all.

So the safe instinct — clear the room, then loot at leisure — pays you nothing. The
points are in grabbing the treasure first and then deciding how much longer you
dare stay in a room that is filling up with your own leavings, with a Hallmonster
due.

**Dying costs the room, not the level.** You are put back in the hall and the room
is still there to try again.

## Between levels

`SCORE THIS LEVEL` × `BONUS MULTIPLIER` = `TOTAL BONUS`, added to your score. The
multiplier is the level you just finished plus the lives you still have, so a clean
level is worth a great deal more than a survived one.

## Sound

The game does not have a score, only cues: a chime for the treasure, a short note
for a kill, a low tone when you die — and one that matters, the note that means a
Hallmonster has just walked into the room you are standing in. You need to know
that without looking away from what you are doing.

## When it ends

Three lives and the run is over. The last screen shows your **final score** and asks
whether you want another go — **Y** for a new game, **N** or **Q** back to the DOS. A
new game starts you over completely, treasure roster included: it is what *this*
player has found.

## Differences from the arcade

- Movement is by whole character cells rather than smooth motion; there is no
  sprite layer on this machine, so cells are the resolution. At 15 cells a second
  it still plays as an arcade game rather than a board game.
- The hall and the rooms are separate screens instead of a zoom, so the hall shows
  each room as a block rather than as its true shape.
- Some monsters and treasures are renamed to match the glyphs the character ROM
  actually has: the Necklace, Urn, Key, Crown and Griffin are a Jewel, an Ingot, a
  Ring, an Amulet and a Wraith here.
- Six room layouts serve the twelve room-visits of a run, dealt four at a time and
  rotated by level, rather than twelve distinct pictures. The roster lists those six.
- No moving walls. One arcade room has bars that slide back and forth; the room that
  takes its shape from that screenshot does not take its motion.

What *is* here because the arcade has it: two doorways per room, looted rooms sealing
themselves solid, the growing Hallmonster count, the per-level recolour, the facing
pip, the treasure roster and the between-levels tally.

What is *not* changed: the invincible Hallmonsters, the lethal remains, the
worthless-before-the-treasure rule, one arrow in flight, and the fact that there is
no ending. You play until your lives run out.
