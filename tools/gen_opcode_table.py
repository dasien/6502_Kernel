#!/usr/bin/env python3
"""Generate the canonical 65C02 opcode/addressing-mode table from the emulator.

The single source of truth for which opcodes exist and how they decode is the
CPU6502 emulator's handler map (src/computer/CPU6502.cpp). That CPU is validated
against the Klaus2m5/amb5l functional, decimal, and 65C02-extended test suites,
so its handler set IS the WDC W65C02S instruction set as this project defines it.

This script parses those handler registrations and emits:
  * src/kernel/assembler/opcodes_65c02.inc - ca65 tables for the assembler/
    disassembler module (Phase 4): per-opcode mnemonic id + mode id, the
    mnemonic strings, and per-mode operand length.
  * docs/opcode_table_65c02.md             - a human-readable reference.

Run with --check to regenerate in memory and fail (exit 1) if the committed
files are stale - this is wired into ctest so the table can never silently
drift from CPU6502.cpp. Regenerate (no --check) after changing CPU opcodes.
"""

import os
import re
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CPU_SRC = os.path.join(REPO_ROOT, "src", "computer", "CPU6502.cpp")
INC_OUT = os.path.join(REPO_ROOT, "src", "kernel", "assembler", "opcodes_65c02.inc")
MD_OUT = os.path.join(REPO_ROOT, "docs", "opcode_table_65c02.md")

# Branch mnemonics use relative addressing; every other zero-suffix handler is
# implied. (The handler name carries no mode suffix for either, so we split them
# here.)
BRANCHES = {"BPL", "BMI", "BVC", "BVS", "BCC", "BCS", "BNE", "BEQ", "BRA"}

# Handler-name mode suffix -> (mode id, asm operand template, byte length).
# The template uses {} for the operand value; %02X / %04X picked by the renderer.
MODE_SUFFIX = {
    "":                         None,  # resolved to IMP or REL by mnemonic
    "Accumulator":              ("ACC", "A",          1),
    "Immediate":                ("IMM", "#${:02X}",   2),
    "ZeroPage":                 ("ZP",  "${:02X}",    2),
    "ZeroPageX":                ("ZPX", "${:02X},X",  2),
    "ZeroPageY":                ("ZPY", "${:02X},Y",  2),
    "ZeroPageIndirect":         ("ZPI", "(${:02X})",  2),
    "IndexedIndirect":          ("IZX", "(${:02X},X)", 2),
    "IndirectIndexed":          ("IZY", "(${:02X}),Y", 2),
    "Absolute":                 ("ABS", "${:04X}",    3),
    "AbsoluteX":                ("ABX", "${:04X},X",  3),
    "AbsoluteY":                ("ABY", "${:04X},Y",  3),
    "Indirect":                 ("IND", "(${:04X})",  3),
    "AbsoluteIndexedIndirect":  ("AIX", "(${:04X},X)", 3),
}
MODE_IMP = ("IMP", "",          1)
MODE_REL = ("REL", "${:04X}",   2)  # disassembler resolves the target address
# Bit ops (added by formula, not parsed): RMBn/SMBn zp, BBRn/BBSn zp,rel.
MODE_ZP_BIT = ("ZP",  "${:02X}",          2)
MODE_ZPREL  = ("ZPR", "${:02X},${:04X}",  3)
# Synthetic modes for undefined opcodes (deterministic multi-byte NOPs on the
# 65C02). Rendered as raw bytes by the disassembler; never emitted by the asm.
MODE_UND = {1: ("UN1", "", 1), 2: ("UN2", "", 2), 3: ("UN3", "", 3)}

# Stable mode ordering for the emitted MODE_LEN table / mode ids.
MODE_ORDER = ["IMP", "ACC", "IMM", "ZP", "ZPX", "ZPY", "ZPI", "IZX", "IZY",
              "REL", "ABS", "ABX", "ABY", "IND", "AIX", "ZPR",
              "UN1", "UN2", "UN3"]
MODE_LEN = {
    "IMP": 1, "ACC": 1, "IMM": 2, "ZP": 2, "ZPX": 2, "ZPY": 2, "ZPI": 2,
    "IZX": 2, "IZY": 2, "REL": 2, "ABS": 3, "ABX": 3, "ABY": 3, "IND": 3,
    "AIX": 3, "ZPR": 3, "UN1": 1, "UN2": 2, "UN3": 3,
}

DIRECT_RE = re.compile(
    r"handlers_\[(0x[0-9A-Fa-f]+)\]\s*=\s*\[this\]\(\)\s*\{\s*handle([A-Za-z0-9]+)\(\);")


def build_table():
    """Return opcode -> (mnemonic, mode_id, length) for all 256 opcodes."""
    with open(CPU_SRC) as f:
        src = f.read()

    # opcode -> (mnemonic, mode_id)
    table = {}

    # 1) Directly registered, single-operation handlers (handle<Mnem><Mode>()).
    for m in DIRECT_RE.finditer(src):
        opcode = int(m.group(1), 16)
        name = m.group(2)
        mnem = name[:3].upper()           # all 6502 mnemonics are 3 letters
        suffix = name[3:]
        mode_info = MODE_SUFFIX.get(suffix)
        if mode_info is None:
            if suffix != "":
                raise SystemExit(f"Unknown mode suffix '{suffix}' in handle{name}")
            # No mode suffix: branches are relative, JSR is the one absolute op
            # whose handler carries no suffix (handleJsr), everything else implied.
            if mnem in BRANCHES:
                mode_info = MODE_REL
            elif mnem == "JSR":
                mode_info = MODE_SUFFIX["Absolute"]
            else:
                mode_info = MODE_IMP
        table[opcode] = (mnem, mode_info[0])

    # 2) Rockwell/WDC bit ops, registered in a loop in CPU6502.cpp:
    #    RMBn $07+n*$10, SMBn $87+n*$10 (zp); BBRn $0F+n*$10, BBSn $8F+n*$10 (zp,rel).
    for n in range(8):
        table[0x07 + n * 0x10] = (f"RMB{n}", MODE_ZP_BIT[0])
        table[0x87 + n * 0x10] = (f"SMB{n}", MODE_ZP_BIT[0])
        table[0x0F + n * 0x10] = (f"BBR{n}", MODE_ZPREL[0])
        table[0x8F + n * 0x10] = (f"BBS{n}", MODE_ZPREL[0])

    # 3) Undefined opcodes -> deterministic NOPs of 1/2/3 bytes (per the WDC
    #    datasheet / CPU6502's NOP-family loops). Fill whatever is left.
    nop1 = ([op for op in range(0x03, 0x100, 0x10)] +
            [op for op in range(0x0B, 0x100, 0x10) if op not in (0xCB, 0xDB)])
    nop2 = [0x02, 0x22, 0x42, 0x62, 0x82, 0xC2, 0xE2, 0x44, 0x54, 0xD4, 0xF4]
    nop3 = [0x5C, 0xDC, 0xFC]
    for op in nop1:
        table.setdefault(op, ("???", MODE_UND[1][0]))
    for op in nop2:
        table.setdefault(op, ("???", MODE_UND[2][0]))
    for op in nop3:
        table.setdefault(op, ("???", MODE_UND[3][0]))

    missing = [op for op in range(256) if op not in table]
    if missing:
        raise SystemExit("Opcodes not covered: " + ", ".join(f"${o:02X}" for o in missing))

    return {op: (mnem, mode, MODE_LEN[mode]) for op, (mnem, mode) in table.items()}


def mnemonic_list(table):
    """Ordered, de-duplicated mnemonics (defined ops first, '???' last)."""
    seen = []
    for op in range(256):
        mnem = table[op][0]
        if mnem not in seen:
            seen.append(mnem)
    # Keep '???' at the end for readability.
    seen = [m for m in seen if m != "???"] + (["???"] if "???" in seen else [])
    return seen


def emit_inc(table):
    mnems = mnemonic_list(table)
    mnem_id = {m: i for i, m in enumerate(mnems)}
    mode_id = {m: i for i, m in enumerate(MODE_ORDER)}
    width = max(len(m) for m in mnems)  # 4 (e.g. "RMB0")

    lines = []
    lines.append("; ===================================================================")
    lines.append("; opcodes_65c02.inc - canonical 65C02 opcode/addressing-mode table")
    lines.append("; ===================================================================")
    lines.append("; AUTO-GENERATED by tools/gen_opcode_table.py from src/computer/CPU6502.cpp.")
    lines.append("; DO NOT EDIT BY HAND. Regenerate after changing CPU6502 opcode handlers;")
    lines.append("; the memory_opcode_table ctest fails if this file is stale.")
    lines.append(";")
    lines.append("; Shared by the Phase 4 assembler and disassembler. Lookups:")
    lines.append(";   disassemble: mnem = OPC_MNEM[opcode]; mode = OPC_MODE[opcode];")
    lines.append(";                length = MODE_LEN[mode].")
    lines.append(";   assemble:    scan OPC_MNEM/OPC_MODE for (mnem,mode) -> index = opcode.")
    lines.append("")
    lines.append(f"MNEM_COUNT = {len(mnems)}")
    lines.append(f"MNEM_WIDTH = {width}")
    lines.append(f"MODE_COUNT = {len(MODE_ORDER)}")
    lines.append("")
    lines.append("; Addressing-mode ids")
    for i, m in enumerate(MODE_ORDER):
        lines.append(f"MODE_{m:<3} = {i}")
    lines.append("")
    lines.append("; Operand byte length per mode id (includes the opcode byte)")
    lines.append("MODE_LEN:")
    lines.append("    .byte " + ", ".join(str(MODE_LEN[m]) for m in MODE_ORDER))
    lines.append("")
    lines.append(f"; Mnemonic strings, {width} bytes each (uppercase, space-padded)")
    lines.append("MNEM_STR:")
    for m in mnems:
        lines.append(f'    .byte "{m:<{width}}"')
    lines.append("")
    lines.append("; Per-opcode mnemonic id (index into MNEM_STR), 256 entries")
    lines.append("OPC_MNEM:")
    lines += _byte_rows([mnem_id[table[op][0]] for op in range(256)])
    lines.append("")
    lines.append("; Per-opcode addressing-mode id, 256 entries")
    lines.append("OPC_MODE:")
    lines += _byte_rows([mode_id[table[op][1]] for op in range(256)])
    lines.append("")
    return "\n".join(lines) + "\n"


def _byte_rows(values, per_row=16):
    rows = []
    for i in range(0, len(values), per_row):
        chunk = values[i:i + per_row]
        rows.append("    .byte " + ", ".join(f"{v:3d}" for v in chunk) +
                    f"   ; ${i:02X}")
    return rows


def emit_md(table):
    defined = sum(1 for op in range(256) if table[op][0] != "???")
    lines = []
    lines.append("# Canonical 65C02 Opcode Table")
    lines.append("")
    lines.append("Auto-generated by `tools/gen_opcode_table.py` from "
                 "`src/computer/CPU6502.cpp` (the validated W65C02S emulator) - "
                 "**do not edit by hand**. This is the single source of truth for the "
                 "Phase 4 assembler/disassembler module. Regenerate after changing "
                 "CPU opcode handlers; the `memory_opcode_table` test fails if stale.")
    lines.append("")
    lines.append(f"Defined opcodes: **{defined}** of 256 "
                 f"(the remaining {256 - defined} are deterministic multi-byte NOPs, "
                 "shown as `???`).")
    lines.append("")
    lines.append("| Opcode | Mnemonic | Mode | Len |")
    lines.append("|--------|----------|------|-----|")
    for op in range(256):
        mnem, mode, length = table[op]
        lines.append(f"| `${op:02X}` | {mnem} | {mode} | {length} |")
    lines.append("")
    return "\n".join(lines) + "\n"


def main():
    check = "--check" in sys.argv[1:]
    table = build_table()
    outputs = {INC_OUT: emit_inc(table), MD_OUT: emit_md(table)}

    if check:
        stale = []
        for path, content in outputs.items():
            existing = open(path).read() if os.path.exists(path) else None
            if existing != content:
                stale.append(os.path.relpath(path, REPO_ROOT))
        if stale:
            print("Opcode table is STALE (CPU6502 changed but table not "
                  "regenerated):\n  " + "\n  ".join(stale), file=sys.stderr)
            print("Run: python3 tools/gen_opcode_table.py", file=sys.stderr)
            return 1
        defined = sum(1 for op in range(256) if table[op][0] != "???")
        print(f"Opcode table current: {defined} defined opcodes, "
              f"{256 - defined} NOPs.")
        return 0

    for path, content in outputs.items():
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            f.write(content)
        print("wrote", os.path.relpath(path, REPO_ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
