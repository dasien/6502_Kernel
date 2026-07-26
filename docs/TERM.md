# TERM — Terminal Manual

**TERM** is MFC's serial terminal: it dials BBSes and IRC-over-telnet servers
through the emulated 6551 ACIA and a host Hayes-modem/telnet bridge, renders
ANSI/VT100 with full CP437, keeps a scrollback buffer, and transfers files with
XMODEM to and from the FAT16 disk.

## Quick reference

| Key | Action |
|-----|--------|
| `^D` | Dial (saved-list menu or `host:port` prompt) |
| `^S` | XMODEM **send** a disk file |
| `^R` | XMODEM **receive** to a disk file |
| `^X` | Hang up (`+++ATH`) |
| `^Q` | Quit to DOS |
| PgUp / PgDn | Scroll back / forward |
| ESC | Cancel the dial prompt; abort a transfer |

## Starting

From the DOS `]` prompt:

```
TERM                 launch, then dial from inside
TERM host:port       launch and immediately dial that address
```

The top line shows the reminder banner:

```
MFC TERM v1.3  ^D dial  ^S/^R xfer  ^X hangup  ^Q quit  PgUp/PgDn scrollback
```

Once connected, everything you type is sent to the remote host; everything it
sends is rendered on screen.

## Dialing (`^D`)

Press **Ctrl-D**:

- If a saved list exists (`SYSTEM/DIAL.LST`), a **Saved BBSes** menu appears —
  press **1–9** to dial an entry, **0** to type an address, or **ESC** to cancel.
- Otherwise you get a `Dial:` prompt. Type an address as **`host:port`** (e.g.
  `bbs.example.com:23`) and press Enter. **ESC** cancels the prompt.

TERM sends `ATDT host:port` to the modem and watches for `CONNECT` / `NO CARRIER`.

### The dial list — `SYSTEM/DIAL.LST`

A plain-text file in the `SYSTEM/` drawer, one BBS per line:

```
# lines starting with '#' and blank lines are ignored
host:port   Display Name
bbs.8-bitarchive.com:2223   8-Bit Archive
```

The first whitespace-delimited token is the `host:port` address; the rest of the
line is the label shown in the menu (optional). Up to **9** entries are listed.
(The IRC client uses `SYSTEM/IRC.LST` in the same format.)

## Hanging up (`^X`)

**Ctrl-X** sends the Hayes escape + hang-up (`+++` then `ATH`), but only while
online, so it won't error when already disconnected.

## Quitting (`^Q`)

**Ctrl-Q** hangs up (if online) and returns to the DOS `]` prompt.

## Scrollback (PgUp / PgDn)

Lines that scroll off the top are kept in a RAM ring buffer. **PgUp** pages back
through history (incoming data is buffered while you review); **PgDn** pages
forward and returns you to the live session.

## File transfer — XMODEM (`^S` / `^R`)

Transfers use the **FAT16 disk in your current drawer**, *not* host files — the
same disk the DOS `CATALOG`/`TYPE`/`COPY` commands see.

- **`^R` — Receive:** prompts `Receive as:`; type a filename. TERM creates/opens
  it on the disk and runs the XMODEM receiver; on success it prints `Received OK`.
- **`^S` — Send:** prompts `Send file:`; type an existing disk filename. If it's
  not found you get `Not found`; otherwise TERM sends it and prints `Sent OK`.

During a transfer the bottom row shows a reverse-video status line with the block
count; **ESC aborts** (sends CAN). The protocol is XMODEM with CRC-16 (falling
back to checksum), 128-byte blocks.

To transfer into a specific drawer, `OPEN` that drawer in DOS before launching
TERM (the file lands in the current directory).

## Display

TERM is an 80×25 ANSI/VT100 terminal:

- Cursor positioning and movement (CUP, CUU/CUD/CUF/CUB), save/restore.
- Erase-in-display and erase-in-line (`ESC[J`, `ESC[K`).
- SGR colors — 16-color foreground/background, bright, reverse (`ESC[...m`).
- Device status / attributes reports (`ESC[6n`, `ESC[c`).
- Full 8-bit **CP437** glyphs, so BBS box-drawing and block art render correctly.

## Serial settings

The ACIA runs at **19200 baud, 8N1** (polled). The host modem bridge maps the
`host:port` you dial to a TCP/telnet connection.
