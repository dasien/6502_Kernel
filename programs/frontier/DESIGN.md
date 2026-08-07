# FRONTIER FORTUNE — Design

A Wild-West trading game for MFC (`FRONTIER.PRG`, cc65). Port of the author's 2008
Objective-C iPhone prototype at `~/Source/repos/Frontier Fortune`, in the
*Taipan!* / *Drug Wars* lineage: buy low, sell high, beat the loan shark.

Menu-driven and turn-based — no real-time loop, no scrolling, no timing. This is
the genre MFC was built to run.

## The run

You start with **$2,000 cash**, **$5,500 debt**, **60 days**, and a wagon holding
**100 units**. Travel between six towns trading eight goods; at day 60 your score
is your net worth. Retire early if you like — the clock is the only thing forcing
you out.

## Goods and prices

Prices re-roll on arrival in every town. The spread is the whole game: with only
100 units of space, bulk goods are worthless per slot, so space pressure pushes
you up-market toward cargo you can't yet afford.

| Good | Price range | | Good | Price range |
|---|---|-|---|---|
| Feed | 10–59 | | Medicine | 500–1,199 |
| Water | 20–149 | | Guns | 1,000–3,499 |
| Food | 30–239 | | Lumber | 5,000–12,499 |
| Whiskey | 300–799 | | Gold | 15,000–29,999 |

## The economy — the one real departure

**Prices are persistent world state, not dice.** `price[NTOWNS][NGOODS]` holds a
live price for every good in every town at all times. Nothing is regenerated on
arrival.

- **Each town has a character.** It may be *short* of a good (its normal price
  sits near the top of that good's range), have a *surplus* (near the bottom),
  both, or neither — a third of slots are empty, so some towns are unremarkable.
  Characters **reshuffle** at ~3%/town/day, so roughly one town changes every 3–4
  days and the map will not hold still.
- **Each day every price drifts**: it closes 1/6 of the gap to its town's normal
  and takes a ±3% random walk. That is the "other people are trading too" that
  keeps an untouched market from standing still.
- **Your trades move the price immediately.** Selling `MARKET_DEPTH`(200)/2 = 100
  units roughly halves it, *on screen, as you sell*. Buying does the reverse. Then
  it heals over about a week.

### Why this replaced the first attempt

The first design rerolled prices across their full range on arrival and layered a
hidden ±60% "pressure" on top. That was self-defeating in two ways: the reroll
noise was far larger than the effect, so the player could never perceive having
caused anything; and pressure was only applied *inside* the reroll, so the price
did not move until the next visit. A `glut` label appeared with no visible cause.

Verified on the host: selling 100 Water at $124 takes it to $69 immediately and it
recovers to $114 over ten days. The two-town shuttle now earns $7.7k on trip one,
$1.4k on trip two, and is **losing $16.6k a trip by trip ten** — it dismantles
itself instead of printing money.

### Information model

Everything you know about a town comes from your **last visit** — prices *and*
character, both snapshotted in `seen_*` on arrival and after each trade. The
travel list shows remembered needs plus how many days stale; the **Ledger** (`L`)
shows the whole remembered price grid. Reading the live values there would be
clairvoyance, and would contradict the point: information rots, so keep moving.

### Genre note

Drug Wars, Dope Wars and Taipan! all allow free any-to-any travel and show prices
only where you stand — both of which this port matches. But they have **no
persistent per-town character at all**, rerolling randomly each visit. A living
economy is a deliberate addition, not a port of anything, and it exists because
pure rerolls punish the player for buying without any way to know where to sell.

## The loan shark

**Debt compounds ~2% per day travelled** (`debt += debt / 50`). Untouched, $5,500
becomes about $17,800 by day 60. The original never charged interest, which left
the game with no pressure at all — this is the change that makes the clock matter.

Savings do **not** earn interest; the Bank is for keeping cash out of reach of
road agents, not for yield.

## Screens

`STATUS` (main menu) · `STORE` buy/sell · `TRAVEL` pick a town · `BANK`
deposit/withdraw · `DEBT` repay · `WAGONS` +20 units for $500 · `CASINO` Hi-Lo ·
`END` score + high-score table.

## Travel and events

Travel costs a day, compounds the debt, re-rolls prices, and has a **20% chance of
an event**: shortcut (no day used), delay (2 days), a single good's price doubled
or halved, all prices ×4, found cargo, **lost cargo**.

## Road agents and the sidearm (new)

A **personal sidearm** is bought once at the general store for $250. It is
entirely separate from the **Guns trade good** — that is freight you haul and
sell like any other cargo; this is the pistol on your hip.

Its only purpose is to give you a **choice** when road agents stop you:

- **Unarmed** — the holdup simply happens. They take cargo or cash and ride off.
- **Armed** — you may **fight** or **hand it over**. Fighting risks a worse loss
  but can drive them off with nothing taken.

This finally uses the prototype's `hasGun` flag, which was declared in `Player`
and never read.

## Score and high scores

Final score = **cash + savings − debt**. Negative is bankruptcy. The top scores
persist to `FRONTIER.SCO` on the FAT16 disk via the DOS file ABI (the pattern
ScottFree uses for saves).

## Engine notes

- **Money must be 32-bit `long`, not `int`.** 100 units of Gold at $29,999 is
  ~$3,000,000 — cc65's 16-bit `int` caps at 65,535, so every cash/debt/price-total
  path overflows if this is missed. The kernel's `K_PRINT_DEC` (`$FF27`) already
  takes a pointer to 4 little-endian bytes, so 32-bit display is a solved problem.
- No float anywhere: percentages are integer division (2% = `/50`).
- Input via `K_READ_LINE` + `K_PARSE_DEC`; menus are single keypresses through the
  keystroke buffer. **Not** the control port — that is for real-time programs.
- Screens redraw wholesale on entry. There is no scrolling and no tick loop, so
  none of KPANIC's diff-rendering machinery is needed.
- Fixed arrays throughout, no `malloc`.
- `.cfg` must use `__STACKSTART__ = $8700` — `$8F00` is inside DOS ROM now.

## Bugs in the prototype, deliberately not carried over

- `getRandomItem()` / `getRandomEvent()` use `rand() % 9 - 1` and `rand() % 11 - 1`,
  which return **-1** and can also run past the end — out-of-bounds at both ends.
- `eLoseCargo` was an empty `case`; implemented here.
- No interest, no day-60 ending, no score — the prototype never closed the loop.
- `customizeDescription:` returned nothing, so the `|X|` item-name placeholder in
  event text never substituted.

## Build path

1. Scaffold: `FRONTIER.PRG`, glue, 32-bit money helpers, STATUS screen.
2. Goods + price rolling + town specialities; STORE buy/sell.
3. TRAVEL: day cost, debt interest, price re-roll, end-of-run trigger.
4. BANK, DEBT, WAGONS.
5. Random events (including lost cargo) + road agents.
6. CASINO Hi-Lo.
7. END screen, score, high-score table on disk.
8. Balance pass; `FRONTIER.TXT` manual on disk beside the game; add to
   `programs/catalog.txt` (which the `disk` target reads).

## Deferred: value-based market depth

**Not implemented — recorded as an option.** `MARKET_DEPTH` is measured in
**units**, so saturation only polices the cheap end of the market. Ten bars of
Gold move a price 10/200 = 5%; a hundred barrels of Water move it 50%. High-value
goods therefore dodge saturation entirely, because you can never afford enough
volume to shift the market.

The fix would be to move the price by **dollar volume traded** rather than unit
count, so $90,000 of Gold hits a market as hard as $90,000 of Water. It closes the
hole at its root and leaves the strategies below intact — leverage would still
work, it just could not be repeated into the same town.

### Why it is worth fixing eventually

Combined with the $100,000 debt ceiling, a single leveraged trade on a crashed
price is game-ending. Measured:

| Trade | Best wagons | Net, one day |
|---|---|---|
| Gold, crashed $9,000 → $22,500 | **0** | **+$133,000** |
| Whiskey, crashed $275 → $550 | 12 | +$82,475 |
| Feed, surplus $20 → $35 | 0 | −$500 |

From a starting net worth of −$3,500, that first row effectively ends the run on
day two. The crash event is only ~0.4% per journey, so it is rare — which is why
this is deferred rather than urgent.

### What the same analysis validated

The tiers above are a genuinely good emergent structure and should survive any
fix. Space binds below **$965/unit** and cash binds above it, so:

- **High value** (Gold, Lumber) — borrow; wagons are wasted, cash is the limit.
- **Mid value** (Whiskey, Medicine, Guns) — borrow *and* buy wagons; both bind.
- **Bulk** (Feed, Water, Food) — neither. Interest on max debt exceeds the whole
  margin, so leverage actively loses money.

That answers the design question of whether borrowing is ever correct: it is, and
knowing *when* is the skill.

## Credits

Original game and design by Brian Gentry (Trestle Development, 2008). This is the
author's own port; no third-party code involved.
