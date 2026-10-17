# MFC Documentation

The documentation is split by audience. For those looking to operate the system, manuals (how to use a program) are `UPPERCASE.md`.
For those looking for a deeper understanding of the design of the system, reference/design/internal docs (how it works) are `lowercase.md`.

## Operational Documentation

| Doc | Covers |
|-----|--------|
| [MONITOR.md](MONITOR.md) | The machine-language monitor: memory, run, load/save, conversion — and the assembler/disassembler, which live in the monitor rather than a separate `ASM` module |
| [DOS.md](DOS.md) | MFC/OS shell: launching programs, files, drawers, disk commands |
| [BASIC.md](BASIC.md) | MFC BASIC (EhBASIC): entering, running, LOAD/SAVE |
| [FORTH.md](FORTH.md) | The fig-FORTH module |
| [EDIT.md](EDIT.md) | The full-screen text editor |
| [TERM.md](TERM.md) | The serial/telnet terminal + XMODEM |
| [IRC.md](IRC.md) | The IRC client |
| [GOPHER.md](GOPHER.md) | The Gopher document browser |
| [SAY.md](SAY.md) | S.A.M., the speech synthesizer: English or phonemes, and the voice settings |

Games are not documented here. A game's manual ships on the disk beside it, as plain
ASCII that the machine itself can `TYPE` (or can be viewed outside the emulator via a text editor).

| Game | Manual | On disk |
|-----|--------|--------|
| VENTURE | `programs/venture/VENTURE.TXT` | `GAMES/VENTURE.TXT` |
| KERNEL PANIC | `programs/kpanic/KPANIC.TXT` | `GAMES/KPANIC.TXT` |
| FRONTIER FORTUNE | `programs/frontier/FRONTIER.TXT` | `GAMES/FRONTIER.TXT` |
| THE SUNLESS VAULT | `programs/vault/VAULT.TXT` | `GAMES/VAULT.TXT` |

## Architecture & Reference

| Doc | Covers |
|-----|--------|
| [architecture.md](architecture.md) | System overview, full memory/zero-page map, the kernel ABI, and the bank-switched module design (the consolidated internals reference) |
| [board.md](board.md) | The virtual chipset drawn as a single-board computer, with bus, chips, I/O decode, interrupt lines, and the line between the emulated machine and the host |
| [opcode_table_65c02.md](opcode_table_65c02.md) | 65C02 opcode table supported by the emulator |
| [sound_design.md](sound_design.md) | SID sound chip design |
| [video_design.md](video_design.md) | VIC video chip design including explanations of its redefinable character set, fine vertical scroll, and sprites |
| [cc65_to_prg.md](cc65_to_prg.md) | Building C programs into `.PRG` files which the emulator can then use |
| [disk_image.md](disk_image.md) | How `disk.img` is built: the catalog, `mkdisk`, and the image geometry |
| [kernel_internals.md](kernel_internals.md) | The kernel boot sequence and reset process including zero-page clear, screen clear, module-window clear, RNG seeding, and handoff to DOS |
| [monitor_internals.md](monitor_internals.md) | The monitor command and how the monitor runs. Sections describe character assignment, jump table, parsing helpers, variable space, help and message systems, plus its execution flows and per-command call trees |
| [dos_internals.md](dos_internals.md) | How MFC-DOS and the FAT16 filesystem are built. This covers the shell, block device layer, directories, and on-disk layout |
| [basic_internals.md](basic_internals.md) | Label glossary for the EhBASIC-derived interpreter. |
| [host_gui.md](host_gui.md) | The Qt front end: windows, rendering of the character plane and sprites, host input |
| [references.md](references.md) | Bibliography with licence notes and projects which inspired parts of this work |
