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

Press any key at the title, and again at each **CAUGHT** or **LEVEL CLEARED**.

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

## The hall

The game opens on the **dungeon hall** — four alcoves off a corridor, each with a
room entrance set into the back of it.

| | |
|---|---|
| `☺` | **Winky** — you |
| `■` | A room entrance. Walk onto it to go in. |
| `▓` | An entrance you have already looted. Nothing left in there. |
| `Φ` | **Hallmonster** — see below |
| `█` | Wall |

**You cannot see into a room from the hall, ever.** The entrance tells you nothing
about what is behind it until you have been inside. That is deliberate: you commit
first and find out second.

**You cannot fire in the hall.** The bow is for rooms.

Clear all four rooms and you descend a level. There are three levels, and then it
starts again — faster. There is no ending; you play until your lives run out.

## Hallmonsters

`Φ` They patrol the hall. They cannot be killed, shot, blocked or reasoned with —
an arrow simply stops on one. They kill you on contact, and they are always walking
toward you.

They are slower than you are, so the hall is survivable if you keep moving. What
they are really there for is the clock: **linger too long in a room and one comes
through the door after you.** You will hear it arrive. There is nothing to be done
about it except leave.

## Inside a room

| | |
|---|---|
| `☺` | **Winky** — you |
| `§ Θ ☼ Ω ¥ Φ` | The room's monsters — one arrow each |
| `♣ ♦ ◘ ■ ◙ ♥` | Its treasure — apples, jewel, ring, ingot, chest, amulet |
| `░` | **Remains** — what is left of something you killed |
| `█` | Wall |
| `↑ ↓ ← →` | Your arrow, in flight |

The bar across the top shows **SCORE**, **LIVES** and **LEVEL**.

Take the treasure, then get out — the doorway you came in by, in the top wall, is
also the way out, and it will not let you leave empty-handed.

**One arrow at a time.** You cannot spray. The shot flies in whatever direction you
last moved, and until it hits something or reaches a wall you are unarmed. Facing
sticks after you let go of the key, so you can back away from a serpent while still
aiming at it — often the only way to fight something that is chasing you.

**The dead do not clear away.** A killed serpent leaves remains, and remains kill
you exactly as dead as the serpent did. Every kill makes the room smaller. Clear a
room carelessly and you can wall yourself off from the treasure, or from the door.

**Scoring is the trap.** The treasure is worth 200 × level. A monster is worth
100 × level — *but only after the treasure is in your hand*. Kill anything before
that and it is worth nothing at all.

So the safe instinct — clear the room, then loot at leisure — pays you nothing. The
points are in grabbing the treasure first and then deciding how much longer you
dare stay in a room that is filling up with your own leavings, with a Hallmonster
due.

**Dying costs the room, not the level.** You are put back in the hall and the room
is still there to try again.

## Sound

The game does not have a score, only cues: a chime for the treasure, a short note
for a kill, a low tone when you die — and one that matters, the note that means a
Hallmonster has just walked into the room you are standing in. You need to know
that without looking away from what you are doing.

## Differences from the arcade

- Movement is by whole character cells rather than smooth motion; there is no
  sprite layer on this machine, so cells are the resolution. At 15 cells a second
  it still plays as an arcade game rather than a board game.
- The map and the rooms are separate screens instead of a zoom.
- Some monsters and treasures are renamed to match the glyphs the character ROM
  actually has: the Necklace, Urn, Key, Crown and Griffin are a Jewel, an Ingot, a
  Ring, an Amulet and a Wraith here.
- Six room layouts serve the twelve room-visits of a run, dealt four at a time and
  rotated by level, rather than twelve distinct pictures.

What is *not* changed: the invincible Hallmonsters, the lethal remains, the
worthless-before-the-treasure rule, one arrow in flight, and the fact that there is
no ending. You play until your lives run out.
