# MFC Documentation

Docs are split by audience.  For those looking to operate the system,  manuals (how to use a program) are `UPPERCASE.md`.
For those looking for a deeper understanding of the design of the system, reference/design/internal docs (how it works) are `lowercase.md`.

## Manuals — how to use it

| Doc | Covers |
|-----|--------|
| [MONITOR.md](MONITOR.md) | The machine-language monitor: memory, run, load/save, conversion — and the assembler/disassembler, which live in the monitor rather than a separate `ASM` module |
| [DOS.md](DOS.md) | MFC/OS shell: launching programs, files, drawers, disk commands |
| [BASIC.md](BASIC.md) | MFC BASIC (EhBASIC): entering, running, LOAD/SAVE |
| [FORTH.md](FORTH.md) | The fig-FORTH module |
| [EDIT.md](EDIT.md) | The full-screen text editor |
| [TERM.md](TERM.md) | The serial/telnet terminal + XMODEM |
| [IRC.md](IRC.md) | The IRC client |

Games are not documented here. A game's manual ships on the disk beside it, as plain
ASCII that the machine itself can `TYPE`. A manual is no use on a web page when you are
sitting at the machine wondering what a key does.

| Game | Manual | On disk |
|-----|--------|--------|
| VENTURE | `programs/venture/VENTURE.TXT` | `GAMES/VENTURE.TXT` |
| KERNEL PANIC | `programs/kpanic/KPANIC.TXT` | `GAMES/KPANIC.TXT` |
| FRONTIER FORTUNE | `programs/frontier/FRONTIER.TXT` | `GAMES/FRONTIER.TXT` |
| The Sunless Vault | `programs/vault/VAULT.TXT` | `SVAULT/VAULT.TXT` |

Those files are ASCII-only and wrap at 79 columns, because they are read on an
80-column CP437 display. That rules out Unicode and markdown alike.
`programs/catalog.txt` is what puts them on the disk, through a `doc` line of the form
`doc = X.TXT -> DIR/X.TXT`.

There is no per-game design document. Each game used to carry a `DESIGN.md`, and all
three were retired in favour of two homes that do not drift away from the thing they
describe.

- The reason a decision was made goes in a comment at the line it affects, so that it
  is in front of whoever is about to change it.
- Cross-cutting history, meaning what was tried and rejected along with the
  measurements, goes in `../TODO.md` under Games.

They were retired because they did not survive contact with the code. A single review
found seven false claims in one of them, including a note saying the game used no
sprites when it had been using them for weeks. The old files are still in git history
if one is ever wanted back.

The Sunless Vault used to be the exception, and instructively so. Its 216-line player
guide was an unshipped `USER_GUIDE.md`, while the only thing a player could read at the
machine was a 29-line `STORY.TXT`. The guide even told you to `TYPE STORY.TXT`, so a
file you could not read was pointing at one you could. Both are now a single
`VAULT.TXT`, story first, on the disk where it is needed.

## Architecture & reference

| Doc | Covers |
|-----|--------|
| [architecture.md](architecture.md) | System overview, full memory/zero-page map, the `$FF00` kernel ABI, and the bank-switched module design (the consolidated internals reference) |
| [board.md](board.md) | The virtual chipset drawn as a single-board computer: bus, chips, I/O decode, interrupt lines, and what backs each chip on the host |
| [opcode_table_65c02.md](opcode_table_65c02.md) | 65C02 opcode table |
| [sound_design.md](sound_design.md) | SID sound-chip design |
| [video_design.md](video_design.md) | Why the VIC used to scroll in whole cells, and the three features that fixed it: a redefinable character set, fine vertical scroll, and sprites |
| [cc65_to_prg.md](cc65_to_prg.md) | Building C programs into `.PRG` files |
| [disk_image.md](disk_image.md) | How `disk.img` is built: the catalog, `mkdisk`, and the image geometry |
| [kernel_internals.md](kernel_internals.md) | How the kernel boots, traced against `kernel.asm`: reset, zero-page clear, screen clear, module-window clear, RNG seeding, handoff to DOS |
| [monitor_internals.md](monitor_internals.md) | Adding a monitor command, and how the monitor runs: character assignment, jump table, parsing helpers, variable space, help and message systems, plus its execution flows and per-command call trees |
| [dos_internals.md](dos_internals.md) | How MFC-DOS and the FAT16 filesystem are built: shell, block device layer, directories, on-disk layout |
| [basic_internals.md](basic_internals.md) | Label glossary for the EhBASIC-derived interpreter |
| [host_gui.md](host_gui.md) | The Qt front end: windows, rendering of the character plane and sprites, host input |
| [references.md](references.md) | Bibliography from the editor/filesystem design work, with licence notes |
