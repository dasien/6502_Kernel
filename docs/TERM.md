# TERM — Terminal Manual

TERM is MFC's serial terminal. It dials BBSes and IRC-over-telnet servers through the
emulated 6551 ACIA and a host Hayes-modem and telnet bridge. It renders ANSI and VT100
with the full CP437 character set, keeps a scrollback buffer, and transfers files with
XMODEM to and from the FAT16 disk.

## Quick reference

| Key | Action |
|-----|--------|
| `^D` | Dial (saved-list menu or `host:port` prompt) |
| `^S` | XMODEM send a disk file |
| `^R` | XMODEM receive to a disk file |
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

TERM opens with a reminder of the hotkeys, in the same shape as IRC's and
GOPHER's opening screens:

```
MFC TERM v1.3   (^Q quits)

  ^D          dial
  ^S / ^R     send / receive XMODEM
  ^X          hang up
  PgUp/PgDn   scrollback
```

Once connected, everything you type is sent to the remote host, and everything it
sends back is rendered on screen.

## Dialing (`^D`)

Press Ctrl-D:

- If a saved list exists at `SYSTEM/DIAL.LST`, a Saved BBSes menu appears. Press 1 to
  9 to dial an entry, 0 to type an address, or ESC to cancel.
- Otherwise you get a `Dial:` prompt. Type an address as `host:port` (e.g.
  `bbs.example.com:23`) and press Enter. ESC cancels the prompt.

TERM sends `ATDT host:port` to the modem and watches for `CONNECT` / `NO CARRIER`.

### The dial list — `SYSTEM/DIAL.LST`

A plain-text file in the `SYSTEM/` drawer, one BBS per line:

```
# lines starting with '#' and blank lines are ignored
host:port   Display Name
bbs.8-bitarchive.com:2223   8-Bit Archive
```

The first whitespace-delimited token is the `host:port` address. Anything after it on
the line becomes the label shown in the menu, and it is optional. Up to nine entries
are listed. The IRC client uses `SYSTEM/IRC.LST` in the same format.

## Hanging up (`^X`)

Ctrl-X sends the Hayes escape followed by the hang-up command, which is `+++` and then
`ATH`. It only acts while you are online, so it will not raise an error when you are
already disconnected.

## Quitting (`^Q`)

Ctrl-Q hangs up if you are online, then returns you to the DOS `]` prompt.

## Scrollback (PgUp / PgDn)

Lines that scroll off the top are kept in a RAM ring buffer. PgUp pages back through
that history, and incoming data is buffered while you review it. PgDn pages forward
again and returns you to the live session.

## File transfer — XMODEM (`^S` / `^R`)

Transfers use the FAT16 disk in your current drawer rather than host files. It is the
same disk that the DOS `CATALOG`, `TYPE` and `COPY` commands see.

- `^R` receives. It prompts with `Receive as:` and you type a filename. TERM creates
  or opens that file on the disk and runs the XMODEM receiver, printing `Received OK`
  when it succeeds. If the disk fills up mid-transfer, the receiver cancels and prints
  `Transfer failed - disk full?` rather than acknowledging blocks it could not store,
  which would have left a silently short file behind.
- `^S` sends. It prompts with `Send file:` and you type the name of a file already on
  the disk. You get `Not found` if there is no such file, and otherwise TERM sends it
  and prints `Sent OK`.

During a transfer the bottom row shows a reverse-video status line with the block
count, and ESC aborts by sending CAN. The protocol is XMODEM with CRC-16, falling back
to a plain checksum, in 128-byte blocks.

To transfer into a particular drawer, `OPEN` that drawer in DOS before launching TERM,
because the file lands in whatever the current directory is.

## Display

TERM is an 80 by 25 ANSI and VT100 terminal. It supports the following.

- Cursor positioning and movement through CUP and CUU, CUD, CUF and CUB, along with
  save and restore.
- Erase-in-display and erase-in-line, which are `ESC[J` and `ESC[K`.
- SGR colour through `ESC[...m`, covering 16 foreground and background colours plus
  bright and reverse.
- Device status and attribute reports, which are `ESC[6n` and `ESC[c`.
- Full 8-bit CP437 glyphs, so BBS box-drawing and block art render correctly.

## Serial settings

TERM programs the ACIA for 19200 baud, 8N1, and polls it. The emulated 6551
does not model a baud rate, so data actually moves as fast as the host supplies
it. The host modem bridge maps the
`host:port` you dial to a TCP/telnet connection.
