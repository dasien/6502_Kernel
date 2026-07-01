/*
 *  scrollback.c -- rendered-line history ring + review pager (see scrollback.h).
 *
 *  History is kept as variable-length lines in two parallel byte pools (chars +
 *  per-cell attrs) addressed by a circular descriptor array. A new line that
 *  would overrun the pool tail wraps the write head back to 0, dropping the
 *  oldest lines whose bytes it reclaims; each stored line therefore stays
 *  contiguous and paints without a mid-line wrap. Sizes are tuned against the
 *  .map -- a .PRG loads at $0800 with room to ~$8700, and IRC's code is small.
 */
#include "scrollback.h"

/* Paint straight through the VIC glue shared with the host program (glue.s). */
extern void vaddr(unsigned int cell);
extern void vputc(unsigned char ch);
extern void vattr(unsigned char a);

#define SB_LINES   256      /* max retained lines (descriptor ring)            */
#define SB_POOL    5000     /* bytes per pool (chars, attrs); ~62-166 lines    */
#define SB_PAD     0x02     /* default attribute (green/black) for blank cells */

static char          pool_c[SB_POOL];   /* glyph bytes                         */
static unsigned char pool_a[SB_POOL];   /* parallel per-cell attribute bytes   */
static unsigned int  desc_off[SB_LINES];
static unsigned char desc_len[SB_LINES];

static unsigned int  first;     /* ring index of the oldest retained line      */
static unsigned int  nlines;    /* number of retained lines (<= SB_LINES)      */
static unsigned int  pool_w;    /* next free byte in the pools                 */
static unsigned int  top;       /* logical index (0=oldest) painted on row 0   */

static unsigned char g_top_row, g_rows, g_cols;

/* Live top: the view offset that puts the newest `rows` lines in the region. */
static unsigned int live_top(void)
{
    return (nlines > g_rows) ? (nlines - g_rows) : 0;
}

void sb_init(unsigned char top_row, unsigned char rows, unsigned char cols)
{
    g_top_row = top_row;
    g_rows = rows;
    g_cols = cols;
    sb_reset();
}

void sb_reset(void)
{
    first = 0;
    nlines = 0;
    pool_w = 0;
    top = 0;
}

void sb_push(const char *chars, const unsigned char *attrs, unsigned char n)
{
    unsigned int slot, end, i;
    int live;

    if (n > g_cols) n = g_cols;              /* never store past the width */
    live = !sb_reviewing();                  /* were we tracking the tail? */

    if (pool_w + n > SB_POOL) pool_w = 0;    /* wrap: keep the line contiguous */
    end = pool_w + n;

    /* Drop the oldest lines whose bytes intersect the region we're about to
       overwrite, and any descriptor slot we need to reuse. */
    while (nlines > 0) {
        unsigned int o = desc_off[first];
        unsigned int l = desc_len[first];
        if (pool_w < o + l && o < end) { first = (first + 1) & (SB_LINES - 1); nlines--; }
        else break;
    }
    if (nlines == SB_LINES) { first = (first + 1) & (SB_LINES - 1); nlines--; }

    for (i = 0; i < n; i++) { pool_c[pool_w + i] = chars[i]; pool_a[pool_w + i] = attrs[i]; }

    slot = (first + nlines) & (SB_LINES - 1);
    desc_off[slot] = pool_w;
    desc_len[slot] = n;
    pool_w = end;
    nlines++;

    /* If we were live, keep tracking the tail; if reviewing, hold the view (the
       new line queues below it -- sb_backlog() will count it). */
    if (live) top = live_top();
}

int sb_reviewing(void)
{
    return top < live_top();
}

unsigned int sb_backlog(void)
{
    unsigned int below = top + g_rows;       /* first line index past the window */
    return (nlines > below) ? (nlines - below) : 0;
}

/* Common tail for the movement verbs: clamp `want` into [0, live_top], store it,
   and report whether the view moved. */
static int move_to(unsigned int want)
{
    unsigned int lt = live_top();
    if (want > lt) want = lt;
    if (want == top) return 0;
    top = want;
    return 1;
}

int sb_pageup(void)
{
    unsigned int step = (g_rows > 1) ? (g_rows - 1) : 1;
    return move_to(top < step ? 0 : top - step);
}

int sb_pagedown(void)
{
    unsigned int step = (g_rows > 1) ? (g_rows - 1) : 1;
    return move_to(top + step);
}

int sb_home(void)
{
    return move_to(0);
}

int sb_end(void)
{
    return move_to(live_top());
}

/* Paint one region row from a stored line (line < nlines) or blank it. */
static void paint_row(unsigned char row, unsigned int line)
{
    unsigned char last = SB_PAD;
    unsigned int j, len = 0, off = 0;

    if (line < nlines) {
        unsigned int slot = (first + line) & (SB_LINES - 1);
        off = desc_off[slot];
        len = desc_len[slot];
    }
    vaddr((unsigned int)(g_top_row + row) * g_cols);
    vattr(SB_PAD);
    for (j = 0; j < len; j++) {
        if (pool_a[off + j] != last) { vattr(pool_a[off + j]); last = pool_a[off + j]; }
        vputc((unsigned char)pool_c[off + j]);
    }
    for (; j < g_cols; j++) {
        if (last != SB_PAD) { vattr(SB_PAD); last = SB_PAD; }
        vputc(' ');
    }
}

void sb_paint(void)
{
    unsigned char r;
    for (r = 0; r < g_rows; r++) paint_row(r, top + r);
}
