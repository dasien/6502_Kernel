/*
 *  scrollback.h -- a rendered-line history ring + review pager, shared by the
 *  streaming full-screen MFC programs (IRC now; TERM later).
 *
 *  KEY MODEL: the VIC only shows an 80x25 window; scrollback is just RAM the
 *  program owns. The host stays pure hardware -- it renders the VIC buffer and
 *  knows nothing of history. A program hands each completed display row to
 *  sb_push() as it scrolls in; when the user pages back, this module repaints
 *  the visible region straight through the VIC glue (vaddr/vputc/vattr), which
 *  it shares with the caller (linked into the same .PRG -- no callbacks).
 *
 *  Live vs. review: while sb_reviewing() is false the caller keeps its own live
 *  path (e.g. a hardware scroll + paint the bottom row). While reviewing, the
 *  caller should NOT scroll the screen -- it just keeps sb_push()ing new lines
 *  into history (they queue below the frozen view; sb_backlog() counts them).
 */
#ifndef MFC_SCROLLBACK_H
#define MFC_SCROLLBACK_H

/* Configure the review region: paint rows top_row..top_row+rows-1, each `cols`
   wide. (cols is also the screen stride; the MFC screen is 80 wide.) */
void sb_init(unsigned char top_row, unsigned char rows, unsigned char cols);

/* Clear all history and return the view to the live tail. */
void sb_reset(void);

/* Record one completed display row (n cells: chars[] + parallel attrs[]). */
void sb_push(const char *chars, const unsigned char *attrs, unsigned char n);

/* Non-zero when the view is scrolled back above the live tail. */
int sb_reviewing(void);

/* Number of newer lines below the visible window (0 when live). */
unsigned int sb_backlog(void);

/* Move the view. Each returns non-zero if the visible window actually changed
   (the caller then calls sb_paint()). */
int sb_pageup(void);      /* older, by one page (rows-1, one line of overlap) */
int sb_pagedown(void);    /* newer, by one page; clamps to the live tail */
int sb_home(void);        /* oldest retained line at the top */
int sb_end(void);         /* snap to the live tail */

/* Repaint the whole visible region from history at the current view. */
void sb_paint(void);

#endif /* MFC_SCROLLBACK_H */
