# GOPHER — Gopher Client Manual

GOPHER is MFC's network document browser. It speaks Gopher (RFC 1436) over the
same emulated 6551 ACIA and host modem bridge that TERM and IRC use, and
presents a server's menus as a list you move through with the arrow keys.

Gopher rather than the web, because the web is effectively all HTTPS now and a
4 MHz 65C02 cannot do a TLS handshake. Gopher has no encryption layer, its
protocol is a request and a response, and its menus map exactly onto an 80x25
screen.

## Quick reference

| Key | Action |
|-----|--------|
| Up / Down | Move the selection |
| Enter, Right | Open the selected item |
| Bksp, Left | Back to the previous page |
| Home | Top of the page |
| PgUp / PgDn | Move the selection a screenful |
| `Q` | Quit to DOS |
| ESC | Quit from the opening prompt; cancel a search |

## Starting

From the DOS `]` prompt:

```
GOPHER
```

After the title card you get the opening screen. Type a host and press Enter, or
press Enter on an empty line for `gopher.floodgap.com`. A host may carry a port,
as in `gopher.example.org:7070`. Without one, port 70 is assumed.

ESC at that prompt returns to DOS.

## Reading a menu

The top line shows where you are, as the host and the current selector. The body
is the menu. The bottom line shows the keys.

Each entry is marked by what it is:

| Mark | Meaning |
|------|---------|
| `/` | A submenu. Enter descends into it |
| `?` | A search. Enter prompts for a term |
| (none) | A text file. Enter displays it |
| white text | An information line, not selectable |

Most Gopher menus carry a good deal of white information text, which is the
server's own commentary rather than anything you can follow.

Up and Down move between selectable entries, skipping the information lines.
Enter or Right opens the highlighted one. Backspace or Left returns to the
previous page, sixteen levels deep, after which the oldest is forgotten.

## Searching

An entry marked `?` is a search index. Enter prompts on the bottom line. Type a
term and press Enter, and the results arrive as an ordinary menu. ESC abandons
the search and leaves you where you were.

## Reading a text file

An unmarked entry is a document. Opening it fills the body with its text, which
you scroll with the arrow keys as if it were a menu. Backspace returns to the
menu you came from.

## Limits

A menu is held in memory, not spooled to disk, so it is capped at 150 entries and
about 10 KB of text. A longer menu is truncated and the status line says so,
and the entries shown still work.

Binary items, Gopher type `9`, are not supported. The modem bridge runs a telnet
filter that escapes a literal `$FF` byte, which is harmless for the 7-bit text
Gopher normally carries but would corrupt a binary transfer.

There is no bookmark list yet. Every session starts from a typed host.

## Connection details

GOPHER uses the same transport as TERM and IRC, the 6551 ACIA to the host modem
bridge, which maps `host:port` to a TCP connection. Each request dials, sends the
selector, reads the response, and hangs up, because Gopher closes the connection
at the end of every response rather than holding it open.
