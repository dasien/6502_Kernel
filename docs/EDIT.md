# EDIT — Text Editor Manual

EDIT is MFC's full-screen text editor. It is a small kilo-inspired editor that
renders straight to the 80×25 screen, edits a document held as heap-allocated
lines, and loads/saves plain-text files on the FAT16 disk in your current
drawer. It has vertical and horizontal scrolling, a reverse-video block cursor,
a status line, and incremental search.

## Quick reference

| Key | Action |
|-----|--------|
| Arrows | Move the cursor (wraps at line ends) |
| Home / End | Start / end of line |
| PgUp / PgDn | Up / down one screenful |
| Enter | Split the line at the cursor |
| Backspace / Delete | Delete the character before the cursor (joins lines) |
| `^S` | Save (prompts `Save as:` if unnamed) |
| `^O` | Open a file (prompts `Open:`) |
| `^F` | Incremental search (arrows step matches, Enter keep, ESC cancel) |
| `^Q` | Quit to DOS (press twice if there are unsaved changes) |
| ESC | Cancel a prompt or search (never quits) |

## Starting

From the DOS `]` prompt:

```
EDIT                 start with a blank document
EDIT filename        open filename for editing (creates it on save if new)
```

`EDIT filename` opens that file from the current drawer on the FAT16 disk.
If the file is not found, EDIT reports `Not found` and starts blank. The name is not
remembered in that case, so a later save will prompt you for one. To edit a file in
another drawer, `OPEN` that drawer in DOS before launching EDIT.

## Editing

Type to insert text at the cursor. Everything works the way you would expect.

- Printable characters are inserted at the cursor and push the rest of the line right.
- Enter splits the line at the cursor, so any text after it moves down to a new line.
- Backspace and Delete both remove the character before the cursor. At the start of a
  line, that joins the line onto the end of the previous one.

Any edit marks the document dirty (an asterisk appears in the status line).

## Moving around

| Key | Moves |
|-----|-------|
| Arrows | Left / right by a character, up / down by a line |
| Left at column 1 | Wraps to the end of the previous line |
| Right at end of line | Wraps to the start of the next line |
| Home / End | Start / end of the current line |
| PgUp / PgDn | Up / down one screenful (24 lines) |

The view scrolls automatically to keep the cursor visible, both vertically and
horizontally, so long lines scroll sideways past the 80-column edge.

## Saving (`^S`)

Press Ctrl-S to save. If the document already has a name (you launched with
`EDIT filename` or previously saved), it writes straight back to that file.
Otherwise you get a `Save as:` prompt. Type a filename of up to 47 characters, which
is enough for a `DRAWER/NAME.EXT` path, and press Enter, or press ESC to cancel.

Files are written to the FAT16 disk in your current drawer, the same disk
the DOS `CATALOG`, `TYPE`, and `COPY` commands see. Every line is terminated
with a newline. On success the status line shows `Saved` and the dirty marker
clears.

A save can fail because the volume filled up partway through or because the file could
not be opened. The status line then shows `Save failed` or `Save failed - disk full?`,
the dirty marker stays set, and the filename is not adopted. Press Ctrl-S again to
retry, after freeing space or with a different name. The document in memory is
untouched either way. Note that the partially written file is left on the disk,
because opening for write truncates the old contents.

## Opening a file (`^O`)

Press Ctrl-O to load a different file. An `Open:` prompt appears, and you type a
filename and press Enter, or press ESC to cancel. The file replaces the current
document, so save first if you have unsaved changes. CRLF line endings are tolerated.
If the file is not found, the status line shows `Not found` and the current document
is left untouched.

## Incremental search (`^F`)

Press Ctrl-F to search. A `Search:` prompt appears and the editor jumps to matches as
you type.

- Typing refines the query. The cursor moves to the first match and the matched text
  is highlighted in reverse video.
- The Down and Right arrows jump to the next occurrence, and Up and Left go to the
  previous one. Search wraps around the document and steps through every match,
  including several on the same line.
- Backspace shortens the query.
- Enter keeps the cursor at the current match and reports `Found` or `Not found`.
- ESC cancels and returns the cursor to where it started.

## Quitting (`^Q`)

Press Ctrl-Q to quit to the DOS `]` prompt. If there are unsaved changes, the first
Ctrl-Q is refused with `Unsaved! ^Q to quit`. Press Ctrl-Q again, with no other key in
between, to quit anyway and lose the changes. Pressing any other key disarms the
guard.

ESC does not quit. It only cancels prompts and search, so a stray ESC cannot throw
away your work.

## The status line

The bottom row (row 25) is a reverse-video status line:

```
MFC EDIT name* L3/12 C7  Saved
```

- The name is the current filename, or `[new]` if the document has never been saved.
  A trailing `*` means there are unsaved changes.
- `L3/12` means the cursor is on line 3 of 12 total lines.
- `C7` means the cursor is in column 7.
- A transient message (`Saved`, `Not found`, `Cancelled`, `No memory`, …)
  appears at the right and clears on the next keystroke.

## Display and limits

EDIT draws an 80 by 25 display in green on black, with its own reverse-video block
cursor. The kernel's hardware cursor is hidden while editing and restored on exit. A
document may hold up to 600 lines, and the length of an individual line is bounded
only by available heap memory. Filenames may be up to 47 characters. If memory runs
out mid-edit, the status line shows `No memory` and the edit is refused rather than
corrupting the document.

A file you open may be too big to hold, either because it has more than 600 lines or
because the heap runs out partway through a line. The status line then shows
`Too big - partial, unnamed` and the filename is deliberately not remembered. Ctrl-S
will prompt you for a name, so you cannot accidentally write the partial document
back over the original and lose the part that was dropped.
