#!/usr/bin/env python3
"""Enforce the BIOS / monitor boundary inside the kernel ROM sources.

kernel.asm is the machine: screen, keyboard, hex and decimal conversion, the
pager, IRQ/NMI, sound, bank launching and the $FF00 ABI. monitor.inc is the
interactive debugger that ships with it. They assemble into one ROM today, so the
assembler cannot tell you when a routine drifts to the wrong side -- a JSR across
the boundary just links. This does.

The direction that matters is BIOS -> monitor. Every such reference is a wire that
has to be cut when the monitor becomes a bank module, because the BIOS cannot call
into an unmapped window. Keeping that list short and explicit is the whole point;
each entry below is a known debt with a stated resolution.

monitor -> BIOS is fine in bulk, but only if it goes through the published $FF00
table, since that is all a separate link unit can reach. Anything else has to be
published or copied into the monitor first, so those are listed too.

Exits non-zero with an explanation on any new crossing.
"""

import re
import sys
from pathlib import Path

# BIOS routines the monitor may call. Published $FF00 entries are read out of the
# jump table itself, so this is only the not-yet-published remainder: each one
# needs an ABI slot or a private copy in the monitor before step 2.
UNPUBLISHED_BIOS_CALLS = {
    "CLEAR_CMD_BUFFER":    "8 bytes; READ_COMMAND_LINE's ESC-cancel path shares it",
    "HEX_PAIR_TO_BYTE":    "2-digit hex parse; K_PARSE_HEX only covers 4 digits",
    "PARSE_DECIMAL_VALUE": "decimal core under K_PARSE_DEC; D: needs it directly",
    "PRINT_MSG_AY":        "4-byte wrapper: STA/STY the pointer then PRINT_MESSAGE",
}

# The wires to cut. Anything here is expected; anything new is a regression.
ALLOWED_BIOS_TO_MONITOR = {
    "MONITOR_COLD":        "the entry point, published at K_MON_ENTRY ($FF1E)",
    "MONITOR_MAIN":        "NMI break-to-monitor lands here; becomes a bank map + jump",
    "RECALL_LAST_COMMAND": "READ_COMMAND_LINE implements '.' recall by calling the "
                           "monitor. Line editing is arguably BIOS -- move it down "
                           "rather than publishing it",
    "FILL_RANGE_CORE":     "boot zeroes the $B000-$DFFF module window with the F: "
                           "fill engine. Needs a small private fill loop in the BIOS",
}

LABEL_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*):")
EQUATE_RE = re.compile(r"^[A-Za-z_]\w*\s*=")
IDENT_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\b")
ABI_RE = re.compile(r"^(K_\w+):\s*JMP\s+(\w+)")


def labels(path):
    out = set()
    for line in path.read_text().splitlines():
        m = LABEL_RE.match(line)
        if m:
            out.add(m.group(1))
    return out


def references(path):
    """Identifiers used in code position (comments and equate LHS excluded)."""
    out = set()
    for line in path.read_text().splitlines():
        code = line.split(";")[0]
        if EQUATE_RE.match(code):
            continue
        out.update(IDENT_RE.findall(code))
    return out


def main():
    if len(sys.argv) != 2:
        print("usage: check_kernel_split.py <src/kernel dir>", file=sys.stderr)
        return 2
    d = Path(sys.argv[1])
    kernel, monitor = d / "kernel.asm", d / "monitor.inc"
    for f in (kernel, monitor):
        if not f.is_file():
            print(f"FAIL: {f} not found", file=sys.stderr)
            return 1

    bios_labels, mon_labels = labels(kernel), labels(monitor)
    published = {m.group(2) for line in kernel.read_text().splitlines()
                 if (m := ABI_RE.match(line))}

    errors = []

    both = bios_labels & mon_labels
    if both:
        errors.append("defined in BOTH kernel.asm and monitor.inc: "
                      + ", ".join(sorted(both)))

    # BIOS must not reach into the monitor beyond the known wires.
    leaks = (references(kernel) & mon_labels) - set(ALLOWED_BIOS_TO_MONITOR)
    if leaks:
        errors.append(
            "kernel.asm (BIOS) references monitor.inc labels that are not known "
            "boundary crossings: " + ", ".join(sorted(leaks)) + ".\n"
            "    The BIOS cannot call into the monitor once it is a bank module -- "
            "the window will not be mapped.\n"
            "    Either keep the routine in the BIOS, or add it to "
            "ALLOWED_BIOS_TO_MONITOR with the resolution you intend.")

    # Monitor may only reach BIOS via $FF00, plus the listed not-yet-published set.
    reachable = published | set(UNPUBLISHED_BIOS_CALLS)
    stray = (references(monitor) & bios_labels) - reachable
    if stray:
        errors.append(
            "monitor.inc calls BIOS routines that are neither published in the "
            "$FF00 table nor listed as unpublished debt: " + ", ".join(sorted(stray))
            + ".\n    A separate link unit can only reach the kernel through $FF00, "
            "so publish it, copy it into the monitor, or record it in "
            "UNPUBLISHED_BIOS_CALLS.")

    if errors:
        print("kernel BIOS/monitor split check FAILED\n", file=sys.stderr)
        for e in errors:
            print("  - " + e + "\n", file=sys.stderr)
        return 1

    unpublished = sorted(references(monitor) & bios_labels & set(UNPUBLISHED_BIOS_CALLS))
    print(f"kernel split OK: BIOS {len(bios_labels)} labels, "
          f"monitor {len(mon_labels)} labels")
    print(f"  BIOS -> monitor crossings to cut for step 2: "
          f"{len(ALLOWED_BIOS_TO_MONITOR)}")
    print(f"  monitor -> BIOS calls still needing an ABI slot: "
          f"{len(unpublished)} ({', '.join(unpublished)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
