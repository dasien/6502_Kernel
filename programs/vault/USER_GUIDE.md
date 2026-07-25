# The Sunless Vault — Player's Guide

*An original text roguelike for MFC. Engine design after the author's **Dungeon
of Yacor**; mechanics inspired by **Telengard** and **Sword of Fargoal**.*

The Shimmering Orb was stolen from the Temple of Dawn and hidden at the bottom of
a fifteen-floor vault, and the world went dark. You are sent down to take it back
and carry it into the light. (The full tale ships in the game's drawer — `OPEN
SVAULT` then `TYPE STORY.TXT`.)

---

## Getting in

The game and its backstory live in the **SVAULT** drawer. At the DOS `]` prompt:

```
OPEN SVAULT
VAULT
```

Roll your hero (press **R** to reroll the four stats, **Enter** to accept), type a
name, and you drop onto floor 1.

## The goal

Descend to **floor 15**, kill the **Vault Guardian**, and step onto the
**Shimmering Orb** to lift it. That opens a stairway and starts a **real-time seal
clock** — climb all the way back to the surface before it runs out. Reaching the
top with the Orb wins; dying or letting the vault seal loses.

## Controls

| Key | Action |
|-----|--------|
| Arrow keys **or** `h` `j` `k` `l` | Move west / south / north / east (bump a creature to attack it) |
| `Space` or `.` | Wait one turn |
| `>` | Use the stairs you're standing on (down, or **up** once you hold the Orb) |
| `i` | Character sheet — your stats, HP/MP, XP, gold, weapon/armor bonuses, and carried potions/scrolls; press a letter to use one |
| `c` | Cast a spell (lists the spells your INT allows) |
| `p` | Pray at an altar (only while standing on one) |
| `Q` | Quit to DOS |

Opening a menu is free; *using* an item or *casting* a spell costs a turn (monsters act). One turn happens per key press.

## What you see (map legend)

| Symbol | Meaning |
|--------|---------|
| `@` (yellow) | You |
| `#` (white) | Wall |
| `.` (green) | Floor |
| `>` (cyan) | Stairs |
| `Ω` (white) | Altar — stand on it and press `p` |
| `δ` (yellow) | The Shimmering Orb (floor 15) |
| a letter | A creature (see the manual below) |
| `¡` | A potion — yellow = healing, cyan = mana |
| `≡` | A scroll (magic map) |
| `☼` | A pile of gold |
| gear glyphs | Weapon upgrade / armor upgrade |
| dark gray | Explored, but out of sight right now (you remember the layout, not what moved) |

You only see what your torch reaches (line-of-sight); rooms light up as you enter,
corridors reveal as you go.

## Your hero

Four stats, rolled at the start (minimum 8) and raised permanently at altars:

- **STR** — melee damage.
- **INT** — which spells you know, and the size of your mana pool.
- **CON** — hit points.
- **DEX** — accuracy and evasion.

- **HP** = `6 + CON + (level−1)·(4 + CON/6)`.
- **Mana** = `2·INT + (level−1)·(INT/2)`.
- **XP / leveling** — kills grant XP; leveling up fully heals you and enlarges both pools. The status bar shows your progress as `XP have/needed`.
- **Natural recovery** — you regain ~1 HP every 15 turns and ~1 mana every 25 turns (HP recovery pauses while poisoned), so you can rest by waiting on a safe tile.
- **Gear** — weapon/armor upgrades add a permanent `+N` to damage / damage-soak.

### Combat

When you bump a creature (or it attacks you), the hit is resolved as:

- **Hit chance** = `70 + attacker accuracy − defender evasion`, clamped to 5–95%.
- **Damage** = a roll in the attacker's range, minus the defender's armor (always at least 1 on a hit).
- **Critical hit** — 10% chance, doubles the damage.

Your accuracy rises with DEX and level; your damage with STR and your weapon
bonus; your armor with the armor bonus (and the Shield spell).

The message log is two lines: the **top** line is what *you* do this turn (e.g.
`You strike the goblin. It is badly hurt!`), the **bottom** line is what the world
does back (`The goblin hits you.`). After a hit that doesn't kill, the top line
hints at the foe's remaining health — *barely hurt*, *wounded*, *badly hurt*, or
*near death* — so you can judge whether to press the attack or back off.

## Monster manual

Creatures appear within a floor range, so the deeper you go the nastier it gets.

| Glyph | Creature | Floors | HP | Notes |
|:---:|----------|:---:|:---:|-------|
| `r` | rat | 1–3 | 4 | weakest thing down here |
| `b` | bat | 1–4 | 4 | hard to hit (evasive) |
| `k` | kobold | 1–4 | 6 | |
| `s` | snake | 2–5 | 7 | **poison** |
| `S` | skeleton | 2–6 | 9 | risen dead |
| `g` | goblin | 2–6 | 10 | |
| `░` | shadow | 3–8 | 12 | **dark and hard to see** — a drifting smudge; evasive |
| `o` | orc | 3–8 | 15 | lightly armored |
| `d` | wolf | 3–8 | 14 | swift hunter |
| `z` | zombie | 3–8 | 20 | slow but tough |
| `G` | gnoll | 4–9 | 18 | |
| `O` | ogre | 5–11 | 30 | hits hard |
| `w` | wraith | 6–12 | 24 | evasive, **poison** |
| `V` | vampire | 7–13 | 38 | **life drain** — for a few turns after it hits, damage you take heals *it* (shown as `DRN`); kill it to end the drain |
| `T` | troll | 7–13 | 42 | **regenerates** — finish it fast |
| `B` | basilisk | 8–14 | 40 | **gaze may paralyze you** for a turn or two |
| `M` | mind flayer | 8–14 | 36 | **may confuse you** — your steps go the wrong way for a few turns |
| `W` | wyvern | 8–14 | 34 | |
| `N` | necromancer | 9–15 | 30 | **raises skeletons** — kill it before it fills the room |
| `D` | demon | 10–15 | 52 | |
| `Y` | drake | 12–15 | 68 | |
| `H` | hydra | 12–15 | 80 | **strikes 1–3 times per turn** |
| `Æ` | **Vault Guardian** | 15 | 110 | the boss; hits hard and regenerates |

**Poison** stacks a few ticks per hit and drains HP each turn until it wears off
(shown as `PSN` on the status bar). **Regenerators** heal a little every turn
they're in view, so burst them down.

Monsters walk *over* gold and items without destroying them — kill one standing on
loot and it drops where it falls.

## Pickups

| Item | Effect                                                                                                          |
|------|-----------------------------------------------------------------------------------------------------------------|
| **Gold** | Currency. Spend it at altars; it also counts toward your score. Picked up automatically; shown on the `i` page. |
| **Potion of healing** | Restores HP.                                                                                                    |
| **Potion of mana** | Restores mana.                                                                                                  |
| **Scroll of magic map** | Reveals a whole floor's layout.                                                                                 |
| **Weapon upgrade** | Permanent +1 to your attack.                                                                                    |
| **Armor upgrade** | Permanent +1 to your defense.                                                                                   |

Potions and scrolls go into your pack (use them from the `i` menu). Gold and gear
apply the instant you step on them.

## Spells (press `c`)

You **know** every spell your INT meets the requirement for; **mana** limits how
often you cast. Casting costs a turn.

| Spell | Min INT | Mana | Effect                                                                                                                     |
|-------|:---:|:---:|----------------------------------------------------------------------------------------------------------------------------|
| **Magic Missile** | 8 | 4 | Bolt the nearest creature you can see (`3 + INT/2 + d4` damage).                                                           |
| **Light** | 9 | 3 | Widens your torch (sight radius) for a while — see farther, not a full map.                                                |
| **Heal** | 10 | 8 | Restore `8 + INT` HP.                                                                                                      |
| **Shield** | 11 | 14 | +armor for several turns.                                                                                                  |
| **Teleport** | 12 | 2 | Blink to a random floor cell — a cheap escape hatch.                                                                       |
| **Time Stop** | 15 | ≥20 (all) | Freezes every monster **and the seal clock** for ~`5 + INT/4` turns. Needs at least 20 mana and consumes your entire pool. |

Raise INT at an altar to unlock more of the book.

## Altars (press `p`)

Now and then a floor holds an **altar** (`Ω`). Stand on it and press `p` to spend gold:

- **Restore body and mind** — refill HP and mana.
- **Blessing of Might / Mind / Vigor / Grace** — a permanent **+1** to STR / INT / CON / DEX (Vigor also raises max HP, Mind your mana pool).

Every purchase raises the price of the next. Your score counts the gold you
*gather*, not what's left in your purse, so spending it at altars costs you nothing
on the scoreboard — buy freely.

## The escape

Lifting the Orb changes everything:

- A **seal clock** appears (`SEAL mm:ss`) and counts down in **real time** — it ticks
  even while you stand still and think, so don't dawdle. It turns **red** in the
  last two minutes.  You have 10 minutes to escape.
- The stairs now lead **up**. Climb floor by floor back toward the surface; each
  floor is freshly generated and **swarming** (every room occupied) — the Orb's
  warmth draws everything to you.
- **Time Stop** freezes the clock as well as the monsters, and opening a menu
  pauses it — use those windows wisely.
- Reach the surface before `0:00` to **win**. If the clock runs out, the vault
  seals you in.

## Score

The end screen tallies:

| Element | Points |
|---------|--------|
| Depth (round-trip: deepest reached + floors climbed back) | ×10 |
| Creatures slain | ×3 |
| Damage dealt | ÷2 |
| Healing done | ÷4 |
| Mana spent | ÷4 |
| Gold gathered (total found, not what's left) | ×1 |
| Escaped with the Orb | +500 |

Getting deep and climbing back out is where the points are — a clean escape is
worth far more than any hoard.

## Tips

- **Rest between fights** — wait on a cleared tile to regen HP/mana before pushing on.
- **Bank an escape** — keep enough mana for a Teleport (only 2), and save Time Stop
  for a swarmed choke point during the climb.
- **Use the altars** — score counts gold *gathered*, not gold kept, so blessings are
  effectively free on the scoreboard; spend on whatever helps you survive the descent.
- **Learn the depth bands** — if you're seeing trolls and wyverns, the ogres and
  gnolls are behind you; plan gear and spells for what's ahead.
