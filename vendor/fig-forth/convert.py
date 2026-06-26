#!/usr/bin/env python3
"""Convert the extracted fig-FORTH 6502 source column to ca65 syntax.

Dialect notes (Ragsdale / MOS-style cross-assembler):
  * Trailing comments have NO ';' — they are just whitespace-separated prose
    after the operand.  6502 operands contain no internal whitespace, so the
    comment begins at the first run of whitespace following the operand token
    (quote-aware: a few .BYTE strings contain embedded spaces).
  * Labels sit in column 0; un-labelled lines start with whitespace.
  * Equates:  LABEL =expr            -> LABEL = expr
  * Location: *=ORIG (drop, linker sets origin) ; *=*+2 -> .res 2
  * Strings:  'LI' (single-quoted)   -> "LI"   (ca65 needs double quotes)
  * .FILE FOR2/1                     -> drop (listing split markers)
  * TOP .END FOR1/1                  -> TOP:   (keep label, drop .END)
  * One unterminated char const: LDA #':  -> LDA #':'

The conversion is verified by re-assembling at $0200 and byte-diffing against
figforth-ref.bin (reconstructed from the listing's object column).
"""
import re
import sys

IMPLIED = {
    'BRK', 'RTI', 'RTS', 'NOP', 'CLC', 'SEC', 'CLD', 'SED', 'CLI', 'SEI',
    'CLV', 'DEX', 'DEY', 'INX', 'INY', 'TAX', 'TAY', 'TSX', 'TXA', 'TXS',
    'TYA', 'PHA', 'PHP', 'PLA', 'PLP',
}


def split_operand_comment(rest):
    """Given the text after the mnemonic (leading spaces already trimmed),
    return (operand, comment).  operand is the first quote-aware token; the
    rest (after whitespace) is comment.  Empty operand if it starts with ';'."""
    if not rest or rest[0] == ';':
        return '', rest
    in_q = False
    for i, ch in enumerate(rest):
        if ch == "'":
            in_q = not in_q
        elif not in_q and (ch == ' ' or ch == '\t'):
            return rest[:i], rest[i:]
        elif not in_q and ch == ';':
            return rest[:i], rest[i:]
    return rest, ''


def convert_quotes(operand):
    """Expand single-quoted literals to a comma-list of single-char ca65
    constants.  Robust for embedded spaces and double-quotes (e.g. the word
    name (." ), and matches how the original emits one byte per char."""
    def expand(m):
        s = m.group(1)
        if s == '':
            return ''
        return ','.join("'" + ch + "'" for ch in s)
    return re.sub(r"'([^']*)'", expand, operand)


def fmt_comment(comment):
    c = comment.strip()
    if not c:
        return ''
    if c.startswith(';'):
        return c
    return '; ' + c


def convert_line(s):
    raw = s.rstrip()
    if raw.strip() == '':
        return ''
    stripped = raw.lstrip()
    # comment-only line
    if stripped.startswith(';'):
        return raw
    has_label = raw[0] not in (' ', '\t')

    label = ''
    body = raw
    if has_label:
        m = re.match(r'(\S+)(\s*)(.*)$', raw)
        label = m.group(1)
        body = m.group(3)
        # equate with attached value: LABEL =expr
        if body.startswith('='):
            expr_rest = body[1:]
            operand, comment = split_operand_comment(expr_rest.lstrip())
            out = f'{label} = {convert_quotes(operand)}'
            c = fmt_comment(comment)
            return out + ('   ' + c if c else '')
    else:
        body = raw.lstrip()

    # location counter directives
    if body.startswith('*='):
        rhs = body[2:].strip()
        oper, comment = split_operand_comment(rhs)
        c = fmt_comment(comment)
        if oper == 'ORIG':
            # linker sets origin; keep as comment marker
            line = '; *=ORIG  (origin set by linker)'
            return line
        if oper.startswith('*+'):
            n = oper[2:]
            line = f'.res {n}'
            return line + ('   ' + c if c else '')
        # generic
        line = f'.org {oper}'
        return line + ('   ' + c if c else '')

    # tokenise mnemonic/directive
    m = re.match(r'(\S+)(\s*)(.*)$', body)
    if not m:
        return raw
    mnem = m.group(1)
    rest = m.group(3)

    # listing-split / end directives
    if mnem == '.FILE':
        return '; .FILE ' + rest.strip()
    if mnem == '.END':
        # keep label as a definition, drop directive
        if label:
            return f'{label}:   ; .END {rest.strip()}'
        return '; .END ' + rest.strip()

    mnem_u = mnem.upper()

    if mnem_u in IMPLIED:
        operand = ''
        comment = rest
    else:
        operand, comment = split_operand_comment(rest)

    # quote conversion for data directives & any char strings
    operand = convert_quotes(operand)

    # assemble label + mnemonic + operand
    pieces = []
    if label:
        pieces.append(label + ':')
    seg = mnem
    if operand:
        seg = seg + ' ' + operand
    pieces.append(seg)
    line = (pieces[0] + ' ' if label else '') + (seg)
    if label:
        line = f'{label}: {seg}'
    else:
        line = seg
    c = fmt_comment(comment)
    if c:
        line = line + '   ' + c
    return line


LOC_RE = re.compile(r'^[0-9A-Fa-f]{4}$')


def extract_source_column(listing_path):
    """Pull the SOURCE column (chars 22+) out of the assembler listing,
    dropping page headers and the LINE#/LOC/CODE columns."""
    out = []
    for raw in open(listing_path, errors='replace'):
        line = raw.rstrip('\n')
        if line.startswith('fig-FORTH for 6502') or line.startswith('LINE # LOC'):
            out.append('')
            continue
        if len(line) < 10 or not LOC_RE.match(line[6:10]):
            out.append('')
            continue
        out.append((line[22:] if len(line) > 22 else '').rstrip())
    return out


def main(src, out):
    # Accept either the raw listing (.lst.txt) or a pre-extracted source file.
    head = open(src, errors='replace').readline()
    if 'fig-FORTH for 6502' in head or head.startswith('LINE # LOC'):
        lines = extract_source_column(src)
    else:
        lines = open(src, errors='replace').read().split('\n')
    res = []
    for ln in lines:
        # the lone unterminated char constant
        if ln.strip() == "LDA #':":
            res.append("        LDA #':'   ; ':' immediate")
            continue
        res.append(convert_line(ln))
    open(out, 'w').write('\n'.join(res) + '\n')
    print(f'converted {len(lines)} lines -> {out}')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'figforth-src-raw.txt',
         sys.argv[2] if len(sys.argv) > 2 else 'figforth.s')
