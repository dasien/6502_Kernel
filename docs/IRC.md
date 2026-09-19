# IRC — Chat Client Manual

IRC is MFC's chat client. It connects to an IRC server through the same
emulated 6551 ACIA and host modem/telnet bridge that TERM uses, then gives you a
scrolling chat window with a pinned input line, a status bar, and scrollback.
Formatting from other users (colour, bold, reverse) renders where the display
can express it, and the rest is stripped so text stays readable.

## Quick reference

| Key / Command | Action |
|---------------|--------|
| Enter | Send the input line (message or command) |
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

Launching with an address skips the server menu for the first connect only.
Later reconnects with `/server` show the normal menu again.

## Connecting — the setup screen

When you launch `IRC` (without an address) you get a full-screen setup flow:

1. Server. If a saved list exists in `SYSTEM/IRC.LST`, an IRC servers menu
   appears. Press 1 through 9 to pick an entry, or 0 to type an address. Typed
   addresses go in the `host:port` form such as `irc.libera.chat:6667`, and a
   blank entry defaults to `irc.libera.chat:6667`. With no saved list you go
   straight to the `Server:` prompt.
2. Nick. Type the nickname you want. A blank entry defaults to `mfcuser`.
3. Channel. Type a channel to join on connect, such as `#mfc`, or leave it
   blank to connect without joining anything.

IRC then shows `Connecting...` and dials via `ATDT host:port`. On success it
registers your nick and joins the channel you named. If the connect fails or is
cancelled you get `Connection failed.` or `Cancelled.` and a prompt to press a
key to retry or ESC to quit.

Pressing ESC at any setup prompt exits to DOS.

### The server list — `SYSTEM/IRC.LST`

A plain-text file in the `SYSTEM/` drawer, one server per line, in the same
format as TERM's `DIAL.LST`.

```
# lines starting with '#' and blank lines are ignored
host:port   Display Name
irc.libera.chat:6667   Libera.Chat
```

The first whitespace-delimited token is the `host:port` address, and the rest of
the line is the label shown in the menu. The label is optional, and the address
is shown when there is no label. Up to nine entries are offered. Edit the file
with the EDIT program.

## Chatting

Once connected the screen splits into three parts:

- The chat region on rows 0 to 22, where messages scroll up as they arrive.
- The input line on row 23, a reverse-video `>` prompt where you type. Because
  it is pinned below the chat region, incoming messages never disturb what you
  are typing.
- The status bar on row 24, which shows `MFC IRC`, your nick, the current
  channel or `(no channel)`, and either `[online]` or `[offline]`.

### Sending messages

Type your message and press Enter. Plain text is sent to the current channel
and echoed as `<yournick> your text`. If you have not joined a channel, you get
the reminder `(no channel - use /join #channel)` instead.

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

Anything you type that starts with `/` is a command, and everything else is a
message to the current channel.

- `/join #channel` joins a channel and makes it current.
- `/part [#channel]` leaves a channel. Bare `/part` leaves the current one.
- `/nick <name>` changes your nickname.
- `/msg <nick> <text>` sends a private message, echoed as `>nick< text`.
- `/me <action>` sends an action to the current channel, echoed as
  `* yournick action`.
- `/list [filter]` lists the channels on the server. The listing can be long,
  and the server may accept a filter such as `>50`.
- `/names [#channel]` lists the users in a channel, the current one if bare.
- `/whois <nick>` looks up a user.
- `/raw <irc command>` sends a raw IRC protocol line as-is.
- `/server`, aliased as `/disconnect`, disconnects and returns to the server
  setup screen so you can connect somewhere else.
- `/quit` quits IRC and returns to the DOS `]` prompt.

An unrecognised `/command` prints the list of valid commands.

If the server reports that your nick is already in use, IRC appends an
underscore and retries, turning `mfcuser` into `mfcuser_`.

## Scrollback (PgUp / PgDn / Home / End)

Every chat line is kept in a RAM history ring:

- PgUp and PgDn page back and forward through history.
- Home jumps to the oldest line and End jumps back to the live tail.

While you are reviewing, the screen holds still and new lines queue below. The
status bar shows `[review +N]`, where N counts the new lines waiting. Pressing
End, or simply typing, snaps you back to the live tail.

## Quitting (`^Q`)

Ctrl-Q, or the `/quit` command, sends a QUIT to the server, hangs up the modem,
and returns you to the DOS `]` prompt. `/server` disconnects the same way but
drops you back at the setup screen instead of exiting.

## Connection details

IRC uses the same transport as TERM, the 6551 ACIA to the host modem bridge,
which maps `host:port` to a TCP connection. It answers server `PING` keepalives
automatically, and responds to CTCP `VERSION` and `PING` requests. A dropped
carrier, reported as `NO CARRIER` or `ERROR`, marks the session `[offline]` and
prints a notice in the chat window.
