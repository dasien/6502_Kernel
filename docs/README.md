# MFC Documentation

Docs are split by audience: **manuals** (how to *use* a program) are `UPPERCASE.md`;
**reference/design/internal** docs (how it *works*) are `lowercase.md`.

## Manuals — how to use it

| Doc | Covers |
|-----|--------|
| [MONITOR.md](MONITOR.md) | The machine-language monitor: memory, run, load/save, conversion |
| [DOS.md](DOS.md) | MFC/OS shell: launching programs, files, drawers, disk commands |
| [BASIC.md](BASIC.md) | MFC BASIC (EhBASIC): entering, running, LOAD/SAVE |
| [ASSEMBLER.md](ASSEMBLER.md) | The DEV-TOOLS assembler/disassembler (`ASM`) |
| [FORTH.md](FORTH.md) | The fig-FORTH module |
| [EDIT.md](EDIT.md) | The full-screen text editor |
| [TERM.md](TERM.md) | The serial/telnet terminal + XMODEM |
| [IRC.md](IRC.md) | The IRC client |

*(Games are self-explanatory; the Sunless Vault keeps its own guide at
`programs/vault/USER_GUIDE.md`.)*

## Architecture & reference

| Doc | Covers |
|-----|--------|
| [ARCHITECTURE.md](ARCHITECTURE.md) | System overview, full memory/zero-page map, the `$FF00` kernel ABI, and the bank-switched module design (the consolidated internals reference) |
| [opcode_table_65c02.md](opcode_table_65c02.md) | 65C02 opcode table |
| [sound_design.md](sound_design.md) | SID sound-chip design |
| [cc65_to_prg.md](cc65_to_prg.md) | Building C programs into `.PRG` files |

## Design / internal (dev)

| Doc | Covers |
|-----|--------|
| [dos_design.md](dos_design.md) | DOS/filesystem design notes |
| [basic_label_glossary.md](basic_label_glossary.md) | EhBASIC internal label map |
| [kernel_flow.md](kernel_flow.md) | Kernel execution flow |
| [kernel_call_tree.md](kernel_call_tree.md) | Kernel routine call tree |
| [kernel_command_infrastructure.md](kernel_command_infrastructure.md) | Monitor command dispatch internals |
| [research_references.md](research_references.md) | External references |
| [UI.md](UI.md) | Host GUI notes |

Assembly examples live in [`../examples/`](../examples/) (see its `README.md`).
