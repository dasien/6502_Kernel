# IRC — Chat Client Manual

**IRC** is MFC's chat client. It connects to an IRC server through the same
emulated 6551 ACIA and host modem/telnet bridge that TERM uses, then gives you a
scrolling chat window with a pinned input line, a status bar, and scrollback.
Formatting from other users (colour, bold, reverse) renders where the display
can express it; the rest is stripped so text stays readable.

## Quick reference

| Key / Command | Action |
|---------------|--------|
| **Enter** | Send the input line (message or command) |
| `^Q` | Quit to DOS |
| PgUp / PgDn | Scroll back / forward through history |
| Home / End | Jump to oldest line / live tail |
| ESC | Cancel/quit from the setup prompts |
| `/join #channel` | Join a channel |
| `/part [#channel]` | Leave a channel (bare = current) |
| `/nick <name>` | Change your nickname |
| `/msg <nick> <text>` | Send a private message |
| `/me <action>` | Send an action to the current channel |
| `/list [filter]` | List channels |
| `/names [#channel]` | List users in a channel |
| `/whois <nick>` | Look up a user |
| `/raw <command>` | Send a raw IRC line |
| `/server` / `/disconnect` | Disconnect and pick another server |
| `/quit` | Quit to DOS |

## Starting

From the DOS `]` prompt:

```
IRC                  launch, then pick a server from the setup screen
IRC host:port        launch and connect straight to that server
```

Launching with an address skips the server menu for the first connect only;
later reconnects (`/server`) show the normal menu again.

## Connecting — the setup screen

When you launch `IRC` (without an address) you get a full-screen setup flow:

1. **Server** — If a saved list exists (`SYSTEM/IRC.LST`), an **IRC servers**
   menu appears. Press **1–9** to pick an entry, or **0** to type an address.
   Typed (or `0`) addresses go in the **`host:port`** form (e.g.
   `irc.libera.chat:6667`); a blank entry defaults to `irc.libera.chat:6667`.
   With no saved list you go straight to the `Server:` prompt.
2. **Nick** — Type the nickname you want. A blank entry defaults to `mfcuser`.
3. **Channel** — Type a channel to join on connect (e.g. `#mfc`), or leave it
   blank to connect without joining anything.

IRC then shows `Connecting...` and dials via `ATDT host:port`. On success it
registers your nick and joins the channel (if you gave one). If the connect
fails or is cancelled you get `Connection failed.` / `Cancelled.` and a prompt
to **press a key to retry** or **ESC to quit**.

**ESC** at any setup prompt exits to DOS.

### The server list — `SYSTEM/IRC.LST`

A plain-text file in the `SYSTEM/` drawer, one server per line, in the **same
format as TERM's `DIAL.LST`**:

```
# lines starting with '#' and blank lines are ignored
host:port   Display Name
irc.libera.chat:6667   Libera.Chat
```

The first whitespace-delimited token is the `host:port` address; the rest of the
line is the label shown in the menu (optional — the address is shown if there is
no label). Up to **9** entries are offered. Edit it with the EDIT program.

## Chatting

Once connected the screen splits into three parts:

- **Chat region** (rows 0–22) — messages scroll up here as they arrive.
- **Input line** (row 23) — a reverse-video `>` prompt where you type. Because
  it is pinned below the chat region, incoming messages never disturb what you
  are typing.
- **Status bar** (row 24) — shows `MFC IRC`, your nick, the current channel (or
  `(no channel)`), and `[online]` / `[offline]`.

### Sending messages

Type your message and press **Enter**. Plain text is sent to the **current
channel** and echoed as `<yournick> your text`. If you have not joined a
channel, you get a reminder: `(no channel - use /join #channel)`.

### How messages appear

- Channel/private messages: `<nick> text`
- Actions (`/me`, CTCP ACTION): `* nick text`
- Notices: `-nick- text`
- Joins/parts/quits/nick changes: `* nick joined …`, `* nick left …`,
  `* nick quit …`, `* nick is now …`
- Server replies (WHOIS, LIST, MOTD, etc.) print as their human-readable text.

Text is down-converted from UTF-8 to the display's single-byte glyph set, and
long space runs (as in padded topics) are collapsed.

## Commands

Anything you type that starts with `/` is a command; everything else is a
message to the current channel.

- **`/join #channel`** — Join a channel and make it current.
- **`/part [#channel]`** — Leave a channel. Bare `/part` leaves the current one.
- **`/nick <name>`** — Change your nickname.
- **`/msg <nick> <text>`** — Send a private message. Echoed as `>nick< text`.
- **`/me <action>`** — Send an action to the current channel (`* you action`).
- **`/list [filter]`** — List channels on the server (can be long; the server
  may accept a filter such as `>50`).
- **`/names [#channel]`** — List the users in a channel (current one if bare).
- **`/whois <nick>`** — Look up a user.
- **`/raw <irc command>`** — Send a raw IRC protocol line as-is.
- **`/server`** (alias **`/disconnect`**) — Disconnect and return to the server
  setup screen to connect somewhere else.
- **`/quit`** — Quit IRC and return to the DOS `]` prompt.

An unrecognised `/command` prints the list of valid commands.

> **Nick already taken?** If the server reports your nick is in use, IRC
> automatically appends `_` and retries (e.g. `mfcuser` → `mfcuser_`).

## Scrollback (PgUp / PgDn / Home / End)

Every chat line is kept in a RAM history ring:

- **PgUp** / **PgDn** page back and forward through history.
- **Home** jumps to the oldest line; **End** jumps back to the live tail.

While you are reviewing, the screen holds still and new lines queue below — the
status bar shows `[review]` (with `+N` for the number of new lines waiting).
Pressing **End**, or simply typing, snaps you back to the live tail.

## Quitting (`^Q`)

**Ctrl-Q** (or the `/quit` command) sends a QUIT to the server, hangs up the
modem, and returns you to the DOS `]` prompt. `/server` disconnects the same way
but drops you back at the setup screen instead of exiting.

## Connection details

IRC uses the same transport as TERM: the 6551 ACIA to the host modem bridge,
which maps `host:port` to a TCP connection. It answers server `PING` keepalives
automatically, and responds to CTCP `VERSION` and `PING` requests. A dropped
carrier (`NO CARRIER` / `ERROR`) marks the session `[offline]` and prints a
notice in the chat window.
