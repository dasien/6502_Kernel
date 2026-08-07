# KERNEL PANIC — Design

A real-time **vertical-scrolling shooter** for MFC (`KPANIC.PRG`, cc65). Original
work; inspired by River Raid, Spy Hunter, and the Xevious-era shmups, rendered in
80×25 CP437 characters (no sprites — the char-cell look is the aesthetic).

## Fiction
You're a defense **trace process** running down a data conduit *inside the machine
itself*, purging corruption sector by sector. Each sector ends at a **firewall
boss** guarding the gate to the next, deeper sector. It's a game about the
computer it runs on.

## Core loop
The data stream flows **down** toward you; you sit near the bottom. Steer
left/right across the conduit, fire **up**, avoid the walls and corruption, top up
energy, and blast the sector boss to descend. Staged (not endless), with a
difficulty ramp and checkpoints at boss gates.

## Signature hook — one shared ENERGY pool
A single `ENERGY` bar does triple duty:
- **Drains slowly over time** — empty = death (River Raid's no-idle-safety tension).
- **Refills** by overlapping **data nodes** in the stream (also shootable for points — the fuel-depot double-role).
- **Powers OVERCLOCK** — hold a key for burst-fire + faster scroll, but it *spends energy fast*.

Every aggressive move gambles against your own lifespan. This fuses the fuel spine
and the weapon tempo into one mechanic instead of two bolted-together systems.

## Controls
`←/→` or `h`/`l` steer · `↑`/`Space` fire · `O` (hold) overclock · `P` pause ·
`Q` quit. Non-blocking read, drain the key queue each frame, **bare ESC never
acts** (held-arrow desync — see the repo's arrow-key note).

## Power-up chain (chosen element)
Primary forward gun always available. Full-wave clears drop **color-coded
fragments**:
- **red** = spread (3 columns) · **blue** = piercing beam (full column) · **green** = homing tracer.

Same color **deepens** (Lv1→3: wider/faster/stronger); a different color **swaps**.
Death drops you to Lv1 of the current color. Color *is* the identity — nearly free
in the attribute engine.

## Bosses (chosen element)
Each sector caps with a multi-cell **firewall**: a corrupted glyph-block with
weak-point cells and 2–3 phases. Clearing it is the **checkpoint** you respawn at.

## Sectors (escalating re-skins)
Same engine, new glyph/palette/tempo per sector: `KERNEL → HEAP → STACK → I/O → …`.
Scroll cadence `N` shrinks (3→1 ticks/row), spawn/fire timers shrink. Cheapest way
to make progress *feel* like progress.

## Enemies
- `▼` daemon — drifts toward you, straight.
- `╫` worm — weaves laterally.
- `♦` sentinel — fixed to the wall, fires an aimed pellet on a timer.
- `•` enemy pellet.

## HUD (pinned rows 22–24; playfield rows 0–21)
`ENERGY` bar · `SCORE` · `WEAPON`+level · `SECTOR` · `LIVES`. Full-width 80-col
conduit; cyan `▓` walls meander (bounded ±1/row random walk) and narrow into
gauntlets.

## Palette (CP437 decimal / MFC attr; `0x40`=bright)
| Element | Glyph | Code | Attr |
|---|---|---|---|
| Craft | `▲` (bank `◄`/`►`) | 30 / 17 / 16 | `0x43` yellow |
| Your bullet | `│` | 179 | `0x47` white |
| Daemon | `▼` | 31 | `0x41` red |
| Worm | `╫` | 215 | `0x45` magenta |
| Sentinel | `♦` | 4 | `0x41` red |
| Enemy pellet | `•` | 7 | `0x41` red |
| Data node | `◘` | 8 | `0x4E` yellow-on-blue |
| Wall | `▓`/`█` | 178/219 | `0x46` cyan |
| Stream | `▒`/`░` | 177/176 | `0x04` blue (phase-dithered) |
| Explosion | `∙→*→☼→▓→░` | 249/42/15/178/176 | white→yellow→red→grey |

## Engine (clone VAULT)
- RAM world model + **shadow-buffer diff renderer** (`vault/draw.c` pattern; attr-latch cache).
- Terrain scrolls via the **chip-side `VREG_CMD` scroll-down** on advance ticks. Order each advance: **erase dynamic objects → scroll terrain + shadow + inject new top row → redraw objects** (skip the erase and you get scrolled-down ghosts).
- **Fixed integer tick loop** off a 60 Hz jiffy counter (see prerequisite); ~12–15 fps. Difficulty = shrinking tick divisors, no floats.
- **Structure-of-arrays** object pools, `unsigned char` coords, inline `rnd16()` xorshift (RTC seed), direct 3-voice SID writes for SFX, honor `SOUND_ENABLE`.
- Collision: `ent[]` occupancy grid (O(1)); **substep** fast bullets cell-by-cell to avoid tunneling.
- Smoothness: half-blocks `▀▄▌▐` for sub-cell edges; **decouple scroll cadence from player-move cadence**.
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
3. Player move/fire + bullets + ENERGY drain/refill + OVERCLOCK.
4. Enemies + wave spawn tables + enemy fire.
5. Power-up chain.
6. Boss + sector progression + checkpoints.
7. Juice (explosions, cell-offset screen-shake, SID cues) + 2-word/BCD score + score screen.
8. Balance; **player manual** as a companion text file shipped on the disk beside
   the game (`KPANIC.TXT`, the way VAULT ships `STORY.TXT` in its drawer) — not a
   `docs/*.md`; disk integration (`programs/catalog.txt` → `GAMES/KPANIC.PRG` +
   `GAMES/KPANIC.TXT`, `ninja disk`).

## References
- `programs/vault/{draw.c,glue.s,vault.c,vault.h}` — renderer, `vaddr/vputc/vattr/vfill/vcmd`, `INCH_NB`, `rnd16()`, real-time loop.
- `docs/ARCHITECTURE.md` — VIC/SID/RTC register ports, attribute byte layout.
- Genre/design research: this session's four research briefs (River Raid, Spy Hunter, genre survey, text-mode techniques).
