#!/usr/bin/env python3
"""Check the kernel BIOS / monitor-module contract.

The monitor is module bank 4 (src/kernel/monitor.asm), linked separately from the
kernel BIOS (src/kernel/kernel.asm). Being separate link units, the assembler now
enforces the hard part for free: kernel.asm cannot name a monitor label and vice
versa, so neither side can accidentally call into the other.

What the assembler CANNOT see is that monitor.asm reaches the BIOS through a list of
hand-written equates to $FF00 addresses:

    PRINT_CHAR = $FF00
    CLEAR_SCREEN = $FF0C
    ...

Nothing ties those numbers to the kernel's actual jump table. Insert an entry in the
middle of the table, or reorder it, and every equate below the insertion point still
assembles perfectly while pointing one slot off -- the monitor would call
GET_KEYSTROKE where it meant PRINT_NEWLINE. That fails at runtime, far from the
edit, with no diagnostic. So: verify every equate against the table it claims to
name, and verify the bank's entry addresses against the constants the kernel jumps
to.

Exits non-zero with an explanation on any mismatch.
"""

import re
import sys
from pathlib import Path

# K_NAME: JMP TARGET  ; $FFxx
ABI_RE = re.compile(r"^(K_\w+):\s*JMP\s+(\w+)\s*;\s*\$(FF[0-9A-F]{2})", re.I)
# NAME = $FFxx   (an ABI equate in the module)
EQU_RE = re.compile(r"^([A-Za-z_]\w*)\s*=\s*\$(FF[0-9A-F]{2})\b", re.I)
# NAME = $xxxx   (any equate, for kernel_vars lookups)
ANY_EQU_RE = re.compile(r"^([A-Za-z_]\w*)\s*=\s*\$([0-9A-F]{1,4})\b", re.I)
ORG_RE = re.compile(r"^\s*\.org\s+\$([0-9A-F]{4})", re.I)
JMP_RE = re.compile(r"^\s*JMP\s+(\w+)")


def read(p):
    return p.read_text().splitlines()


def main():
    if len(sys.argv) != 2:
        print("usage: check_kernel_split.py <src/kernel dir>", file=sys.stderr)
        return 2
    d = Path(sys.argv[1])
    kernel, monitor, varsinc = d / "kernel.asm", d / "monitor.asm", d / "kernel_vars.inc"
    for f in (kernel, monitor, varsinc):
        if not f.is_file():
            print(f"FAIL: {f} not found", file=sys.stderr)
            return 1

    klines, mlines, vlines = read(kernel), read(monitor), read(varsinc)
    errors = []

    # --- the published table: target name -> address -------------------------
    table = {}
    for line in klines:
        m = ABI_RE.match(line)
        if m:
            table[m.group(2).upper()] = "$" + m.group(3).upper()
    if not table:
        errors.append("no $FF00 jump table entries found in kernel.asm -- the "
                      "K_NAME: JMP TARGET ; $FFxx format must have changed")

    # --- every ABI equate in the module must match that table ---------------
    equates, checked = {}, 0
    for i, line in enumerate(mlines, 1):
        m = EQU_RE.match(line)
        if not m:
            continue
        name, addr = m.group(1).upper(), "$" + m.group(2).upper()
        equates[name] = addr
        # Aliases (two names for one entry) are fine as long as the address is real.
        if name in table:
            checked += 1
            if table[name] != addr:
                errors.append(
                    f"monitor.asm:{i}: {name} = {addr}, but kernel.asm publishes it "
                    f"at {table[name]}.\n    The monitor would call whatever now "
                    f"lives at {addr}.")
        elif addr not in table.values():
            errors.append(
                f"monitor.asm:{i}: {name} = {addr}, which is not any entry in the "
                f"kernel's $FF00 table.\n    Either the routine is not published or "
                f"the address is stale; a separate link unit can only reach the "
                f"kernel through that table.")

    # --- bank entry addresses must agree with what the kernel jumps to ------
    consts = {}
    for line in vlines:
        m = ANY_EQU_RE.match(line)
        if m:
            consts[m.group(1).upper()] = int(m.group(2), 16)
    for want in ("MON_BANK", "MON_ENTRY_COLD", "MON_ENTRY_BREAK"):
        if want not in consts:
            errors.append(f"kernel_vars.inc does not define {want}")
    if "MON_ENTRY_COLD" in consts:
        org = next((int(m.group(1), 16) for line in mlines
                    if (m := ORG_RE.match(line))), None)
        if org is None:
            errors.append("monitor.asm has no .org directive")
        elif org != consts["MON_ENTRY_COLD"]:
            errors.append(
                f"monitor.asm .org is ${org:04X} but MON_ENTRY_COLD is "
                f"${consts['MON_ENTRY_COLD']:04X}. The kernel jumps to the constant, "
                f"so the bank would be entered at the wrong address.")
        # The entry table is the first two instructions: JMP cold, JMP break.
        jmps = [m.group(1) for line in mlines[mlines.index(next(
            l for l in mlines if ORG_RE.match(l))):] if (m := JMP_RE.match(line))]
        if len(jmps) < 2:
            errors.append("monitor.asm: expected two JMPs (cold, break) at the base "
                          "of the bank")
        elif "MON_ENTRY_BREAK" in consts:
            gap = consts["MON_ENTRY_BREAK"] - consts["MON_ENTRY_COLD"]
            if gap != 3:
                errors.append(
                    f"MON_ENTRY_BREAK is {gap} bytes past MON_ENTRY_COLD; the entry "
                    f"table is two 3-byte JMPs, so it must be 3.")

    if errors:
        print("kernel BIOS/monitor contract check FAILED\n", file=sys.stderr)
        for e in errors:
            print("  - " + e + "\n", file=sys.stderr)
        return 1

    print(f"kernel/monitor contract OK: {len(table)} published ABI entries, "
          f"{checked} verified against monitor.asm equates")
    print(f"  monitor bank {consts.get('MON_BANK')} entered at "
          f"${consts.get('MON_ENTRY_COLD', 0):04X} (cold) / "
          f"${consts.get('MON_ENTRY_BREAK', 0):04X} (NMI break)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
