# ASSEMBLER — moved

The assembler and disassembler are part of the **monitor** now, not a separate
`ASM` module. See **[MONITOR.md](MONITOR.md)**:

- [Assembler commands](MONITOR.md#assembler-commands) — `A:` `B:` `D:` `L:`
- [Writing source for `B:`](MONITOR.md#writing-source-for-b) — labels, constants,
  expressions, directives, diagnostics
- [Memory used](MONITOR.md#memory-used) — source buffer and symbol table
- [Example programs](MONITOR.md#example-programs)

**Why the change.** The monitor had a memory examiner and a byte writer; the
assembler module had a disassembler and a line assembler. That is one tool split
in half, and the split only made sense while the monitor was resident in kernel
ROM and always underneath you. Once the monitor moved into a bank of its own,
building and testing meant crossing the DOS twice per iteration and losing the
source buffer each time. Period monitors — Supermon, HESMON, the Apple II ROM
monitor and its mini-assembler — kept them together, and so does this.

Identifiers may be up to **16 characters**, and `.BYTE` accepts quoted strings as
well as numbers — both raised in v0.9, because the shipped examples used longer
names and `.byte "TEXT", 0` and so would not assemble.

Note the commands take the monitor's colon grammar now (`D:0800`, not `D 0800`),
and `D:` is the disassembler, so base conversion moved to `#:nnnnn` (decimal to
hex) and `$:xxxx` (hex to decimal).

This file is kept as a pointer because existing notes and links refer to it.
