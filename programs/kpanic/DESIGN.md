# KERNEL PANIC — Design

A real-time **vertical-scrolling shooter** for MFC (`KPANIC.PRG`, cc65). Original
work; inspired by River Raid, Spy Hunter, and the Xevious-era shmups, rendered in
80×25 CP437 characters — the char-cell look is the aesthetic. The craft and its shots are
**hardware sprites**, because anything in the cell plane rides the fine-scroll offset and
so appeared to bob down and snap back; the terrain and enemies stay in the cell plane
where riding the scroll is exactly what they want.

## Fiction
You're a defense **trace process** running down a data conduit *inside the machine
itself*, purging corruption as you go deeper. It's a game about the computer it runs on.

## Core loop
The data stream flows **down** toward you; you sit near the bottom. Steer
left/right across the conduit, fire **up**, avoid the walls and corruption, top up
energy at data nodes, and shoot open the firewalls that bar the channel.

**Endless — the game is how far you get.** There is no ending, no boss and no
checkpoint; `DIST` is the real score. That replaced a staged design with a boss at each
sector gate, for the reason River Raid is built the way it is: a barrier you can either
open or pay to crash through keeps the run going, where a boss stops the world and turns
a scrolling game into a set-piece fight it is not shaped for.

## Signature hook — one shared ENERGY pool
A single `ENERGY` bar does double duty:
- **Drains as you travel** — empty = death (River Raid's no-idle-safety tension).
  Charged per *row*, not per tick, so it is a distance budget and speed is neutral.
- **Refills** by overlapping **data nodes** in the stream (also shootable for points — the fuel-depot double-role).
- **Absorbs every mistake** — crashes, collisions and pellets are all paid out of the
  same bar, so damage and fuel are one resource. River Raid keeps them apart (a crash
  costs a *life*); fusing them is the deliberate difference, and it means mistakes are
  the dominant consumer and have to be priced as setbacks rather than as thirds of a
  tank. See the pricing note in `kpanic.h`.

**OVERCLOCK was cut.** Hold-to-burst existed and it did not earn its keep: it bought
speed you did not want in a game about precision, and its only real effect was a
second cost centre on the energy bar. Removing it also made "one step = one row" a
structural invariant instead of a variable (`WORLD_STEP`).

## Controls
`←`/`→` steer (≈22.5 columns/sec, sub-cell) · `Space` fire · `↑`/`↓` speed · `P` pause ·
`Q` quit.
Steering and firing are read from the PIA key-state port (`$FE0F`), not the keystroke
buffer, so they are simultaneous and have no auto-repeat stall. Non-blocking read,
drain the key queue each frame, **bare ESC never acts** (held-arrow desync — see the
repo's arrow-key note).

## Power-up chain (chosen element)
Primary forward gun always available and unlimited. A kill has a `1-in-FRAG_CHANCE`
chance of dropping a **fragment** (`FRAG_W` cells wide, the footprint of the body that
dropped it):
- `S` = spread (3 columns, 5 at Lv3) — **close range**: its shots burn out at
  `SPREAD_FLOOR` instead of crossing the whole band, dimming for their last two rows so
  it reads as running out of reach rather than blinking out · `B` = beam (a `BEAM_CELLS`-tall bolt at double
  speed, piercing) · `H` = homing tracer.
  Homing picks the nearest target by **Manhattan** distance (`dy + |dx|`) and counts the
  **firewall port** as a target. It used to rank by row distance alone and ignore ports
  entirely, which made it *worse* than the plain gun at the one target you are obliged to
  hit: any enemy on screen captured the shot and dragged it off the port, often chasing
  something too far to the side to ever reach. Data nodes are deliberately not targets —
  shooting one forfeits its refill.

The **letter** carries the identity, not the colour. Colour was the original plan and it
was wrong: red already means corruption and green already means a data node, so a third
colour-coded meaning is one more than the eye can hold mid-dodge. All three fragments
share `A_FRAG`.

The beam was a plain shot that merely didn't die on impact — mechanically distinct,
visually identical, so it read as a bug. It is now a tall bolt built from a stack of
ordinary pool shots in one column, needing no new object kind: the pool's
all-or-nothing volley rule already refuses a partial bolt exactly as it refuses a
partial spread.

Spread's short range began as a sprite-pool problem and became the weapon's identity.
One sprite per shot leaves 16 after the craft, and Lv3 put five up every 3 steps with an
11-step flight — 18 in flight against a pool of 16, so about one press in five was
refused. Not lost shots (a refused volley retries next tick) so the gun already self-paced
to ~cooldown 3.7; the defect was that the cap was *irregular*, so it stuttered, and
invisible to player and reader alike. A fixed cooldown of 4 was measured and rejected —
150 volleys against the broken version's 164, firing *less* while looking like a fix.
Enlarging the chip's sprite block would also have worked (20 is the threshold; 19 buys
nothing) at half the remaining I/O page. Short range fits by construction and buys a real
trade-off instead of a compromise: spread clears crowds close in, the beam reaches, homing
tracks. Measured at floor 6: peak 15 of 16, 200 volleys, none refused.

**A special weapon is a magazine of `W_AMMO` rounds, not a permanent upgrade.** One
trigger pull spends one round whatever the volley's shape, so a five-shot spread and a
four-cell bolt cost the same as a single plain shot — counting projectiles instead
would tax the weapon whose whole point is being wide. Empty reverts to the plain gun.
Without a limit, collecting homing once meant never firing anything else again, which
collapsed the entire chain into one decision made in the first thirty seconds.

Same kind **refills and deepens** (Lv1→3: wider/faster); a different kind **swaps** to
it at Lv1 with a full magazine. A crash drops you one level, not to Lv1.

## Firewalls (River Raid's bridge)
A barrier across the full channel with a single **port** in it, every `FW_ROWS` (80) rows
— the original's cadence, not a guess: its section is 16×32 = 512 scanlines against a
160-line display, so a bridge lands every ~3.2 screens, and 3.2 × `PLAY_H` = 77.

Implemented as **terrain**, one flagged row in the same ring the walls live in, so it
rides the hardware scroll for free and `blocked()` already stops the craft on it. Shoot
the port (`FW_PORT_HP` hits) and the whole barrier goes; fly into it and pay `FW_CRASH`.
A toll, not a gate.

## Sectors — density, never tempo
`KERNEL → HEAP → STACK → I/O → …`, every `SECTOR_ROWS` rows. Escalation is **entirely
density-shaped**, matching what the original actually does:

The **board outside the conduit is the sector's colour** — green, amber, magenta, grey.
That is the zone cue: the banner names the sector but scrolls past in three seconds,
after which every zone looked identical. The colour is stamped onto each row as it is
generated and rides the ring with it, so a new zone *sweeps down* the screen over the
band's depth rather than snapping, and erasing an object on an older row repaints it in
that row's own colour rather than speckling the new one across the transition. The
channel's recessed board deliberately does not change — the lane you fly in should read
the same everywhere.

| dial | sector 1 → 4 |
|---|---|
| enemy spawn interval (rows) | 18 → 8 |
| node scarcity (1-in-N rows) | 55 → 100 |
| sentinel fire interval | 14 → 7 |
| enemy roster | daemon+worm → all three |
| channel width floor | `HW_MIN_EASY`, no gauntlets → unlocked |

**Scroll speed is not on that list.** It used to be (`sector_px` 2→5), which is the one
axis River Raid deliberately leaves alone: its `speedY` is written only by the joystick
and `level` never touches it. Speed is the player's throttle — faster covers more rows
per unit energy (drain is per row) so it scores faster, paid for in reaction time.
Nothing but the arrow keys may write `speed_px`.

## Enemies
- `▼` daemon — drifts toward you, straight.
- `╫` worm — weaves laterally.
- `♦` sentinel — fixed to the wall, fires an aimed pellet on a timer.
- `•` enemy pellet.

## HUD (ONE pinned row, 24; playfield rows 0–23)
`PWR` bar + number · `SCORE` · `DIST` · `WPN` + level + rounds left · `PAUSE` · `S`ector.
No lives — there is only the energy pool, so there is nothing to count. It was three
rows once, which pushed the craft five physical rows off the bottom and made it look like
it was flying mid-screen; the key legend those rows carried lives on the title screen now.
Full-width 80-col conduit; cyan `█` walls meander (bounded ±1/row random walk) and narrow
into gauntlets.

## Palette (CP437 decimal / MFC attr; `0x40`=bright)
| Element | Glyph | Code | Attr |
|---|---|---|---|
| Craft | `▲` | 30 | `0x43` yellow (a sprite, not a cell) |
| Your bullet | `↑` | 24 | `0x47` white |
| Daemon | `▼` | 31 | `0x41` red |
| Worm | `╫` | 215 | `0x45` magenta |
| Sentinel | `♦` | 4 | `0xC1` REVERSED red |
| Enemy pellet | `•` | 7 | `0x41` red |
| Data node | `◘` | 8 | `0x42` bright green |
| Wall / bevel | `█`/`▓` | 219/178 | `0x46` cyan / `0x06` cyan |
| Firewall / port | `▒`/`■` | 177/254 | `0x01` dim red / `0xC7` REVERSED white |
| Board traces | `─│┼○` | 196/179/197/9 | `0x02` green, `0x40` grey recessed |
| Island | cyan shores + board interior | — | `0x46` + `0x02` green |
| Debris | `☼→∙→·` | 15/249/250 | white→red→grey, by remaining ticks |
| Weapon fragment | framed chip, letter across the seam | 16–21 (**soft font**) | `0x67` white on blue |

## Engine (clone VAULT)
- RAM world model + **shadow-buffer diff renderer** (`vault/draw.c` pattern; attr-latch cache).
- Terrain scrolls via the **chip-side `VREG_CMD` scroll-down** on advance ticks. Order each advance: **erase dynamic objects → scroll terrain + shadow + inject new top row → redraw objects** (skip the erase and you get scrolled-down ghosts).
- **Fixed integer tick loop** off a 60 Hz jiffy counter (see prerequisite), no floats. Difficulty is *density*, not tick divisors — see Sectors.
- **Structure-of-arrays** object pools, `unsigned char` coords, inline `rnd16()` xorshift (RTC seed), direct 3-voice SID writes for SFX, honor `SOUND_ENABLE`.
- Collision: `ent[]` occupancy grid (O(1)); **substep** fast bullets cell-by-cell to avoid tunneling.
- Smoothness: **scroll cadence and player-move cadence are decoupled** — done. The world
  advances on the fine-scroll accumulator (a row when it wraps a cell); the craft steers
  every frame in pixels via `spr_x_px`, so lateral speed is independent of the throttle
  instead of being one column per row. Collision still resolves on the cell grid, with
  the craft's column derived from its sprite *centre*.
- Budget: ~30–40 moving objects, ~6 KB working set — comfortable.

## Prerequisite (kernel) — build step 0
No readable monotonic clock exists today (only 1 Hz RTC). Add a **60 Hz monotonic
jiffy counter** to the kernel ABI (a `K_GET_JIFFIES` entry or a ZP word), ticked by
the existing timer IRQ. Bump `kernel.asm` Version and `dos.asm` `DOS_VERSION`.
Benefits every future real-time program.

## Build path
0. Kernel jiffy counter + ABI (+ version bumps).
1. Scaffold `KPANIC.PRG`: glue from VAULT, tick loop, blank scroll region + HUD.
2. Conduit terrain gen + scroll + wall collision.
3. Player move/fire + bullets + ENERGY drain/refill.
4. Enemies + wave spawn tables + enemy fire.
5. Power-up chain.
6. ~~Boss + checkpoints~~ → firewalls + sector progression. The boss was cut; see
   Core loop for why.
7. Juice (explosions, cell-offset screen-shake, SID cues) + 2-word/BCD score + score screen.
8. Balance; **player manual** as a companion text file shipped on the disk beside
   the game (`KPANIC.TXT`, the way VAULT ships `STORY.TXT` in its drawer) — not a
   `docs/*.md`; disk integration (`programs/catalog.txt` → `GAMES/KPANIC.PRG` +
   `GAMES/KPANIC.TXT`, `ninja disk`).

## References
- `programs/vault/{draw.c,glue.s,vault.c,vault.h}` — renderer, `vaddr/vputc/vattr/vfill/vcmd`, `INCH_NB`, `rnd16()`, real-time loop.
- `docs/ARCHITECTURE.md` — VIC/SID/RTC register ports, attribute byte layout.
- Genre/design research: this session's four research briefs (River Raid, Spy Hunter, genre survey, text-mode techniques).
