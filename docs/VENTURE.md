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

Press any key at the title, and again after each room.

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

## What you are looking at

| | |
|---|---|
| `☺` | **Winky** — you |
| `§` | **Serpent** — hunts you, dies to one arrow |
| `♣` | **Apples** — the treasure in this room |
| `░` | **Remains** — what is left of something you killed |
| `█` | Wall |
| `↑ ↓ ← →` | Your arrow, in flight |

The bar across the top shows **SCORE**, **LIVES** and **LEVEL**.

## How it goes

Take the treasure, then get out. The room's door is the top edge.

**One arrow at a time.** You cannot spray. The shot flies in whatever direction you
last moved, and until it hits something or reaches a wall you are unarmed. Facing
sticks after you let go of the key, so you can back away from a serpent while still
aiming at it — often the only way to fight something that is chasing you.

**The dead do not clear away.** A killed serpent leaves remains, and remains kill
you exactly as dead as the serpent did. Every kill makes the room smaller. Clear a
room carelessly and you can wall yourself off from the treasure, or from the door.

**Scoring is the trap.** The treasure is worth 200 × level. A serpent is worth
100 × level — *but only after the apples are in your hand*. Kill anything before
that and it is worth nothing at all.

So the safe instinct — clear the room, then loot at leisure — pays you nothing. The
points are in grabbing the treasure first and then deciding how much longer you
dare stay in a room that is filling up with your own leavings.

## Coming

This is the first room of a planned twelve. Still to come: the dungeon map you move
between rooms on, the **Hallmonsters** that patrol it — invincible, and the reason
you cannot dawdle — the other five monsters and treasures, and the level loop that
speeds everything up and never ends. See `programs/venture/DESIGN.md`.

## Differences from the arcade

- Movement is by whole character cells rather than smooth motion; there is no
  sprite layer on this machine, so cells are the resolution. At 15 cells a second
  it still plays as an arcade game rather than a board game.
- The map and the rooms are separate screens instead of a zoom.
- Some monsters and treasures are renamed to match the glyphs the character ROM
  actually has: the Necklace, Urn, Key, Crown and Griffin are a Jewel, an Ingot, a
  Ring, an Amulet and a Wraith here.

What is *not* changed: the invincible Hallmonsters, the lethal remains, the
worthless-before-the-treasure rule, one arrow in flight, and the fact that there is
no ending. You play until your lives run out.
