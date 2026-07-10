/* ============================================================================
 * The Sunless Vault -- an MFC text roguelike.  PHASE 1: vertical slice.
 *
 * Proves the pipeline and MEASURES the .PRG size: procedural map generation, an
 * integer field-of-view, rendering to the 80x25 CP437 screen, player movement,
 * one monster type with greedy AI + bump combat, and descending stairs.
 * Stats/spells/items/the Orb/escape come in later phases.
 *
 * Original game (design after the author's Dungeon of Yacor; inspired by
 * Telengard / Sword of Fargoal). Talks to MFC only through glue.s (platform).
 *
 * Rendering is windowed: only the cells that can change between turns (a box
 * around the player, since the move step and FOV radius are small) are repainted,
 * and row pointers avoid cc65's per-cell y*80 multiply -- otherwise a full 1840-
 * cell repaint per keypress is far too slow at ~1 MHz.
 * ==========================================================================*/

/* ---- platform (glue.s) ---- */
extern unsigned char INCH(void);
extern int           INCH_NB(void);
extern void          QUITDOS(void);
extern void          vaddr(unsigned int cell);
extern void          vputc(unsigned char ch);
extern void          vattr(unsigned char a);
extern void          vfill(unsigned char ch);       /* fill char for chip block ops */
extern void          vcmd(unsigned char cmd);        /* chip-side clear / fill-row */
extern void          vhidecur(void);

#define VCMD_CLEAR   0x01
#define VCMD_FILLROW 0x04

/* ---- layout: rows 0..22 map, 23 message, 24 status ---- */
#define MAP_W    80
#define MAP_H    23
#define MSG_ROW  23
#define STA_ROW  24

#define T_WALL   0
#define T_FLOOR  1
#define T_STAIRS 2

/* color attributes ([R:7][BR:6][bg:5-3][fg:2-0]) */
#define A_WALL   0x47   /* bright white */
#define A_FLOOR  0x02   /* green */
#define A_DIM    0x04   /* blue -- explored but not currently visible */
#define A_PLAYER 0x43   /* bright yellow */
#define A_MON    0x41   /* bright red */
#define A_STAIRS 0x46   /* bright cyan */
#define A_TEXT   0x07   /* white */

#define MAX_MON   16
#define MAX_ROOMS 14

static unsigned char gmap[MAP_H][MAP_W];
static unsigned char expl[MAP_H][MAP_W];
static unsigned char vis[MAP_H][MAP_W];
static unsigned char shown[MAP_H][MAP_W];  /* last view-code drawn per cell (diff render) */
static unsigned char occ[MAP_H][MAP_W];    /* live-monster index+1 at cell, else 0 */

static signed char px, py;
static int         php, pmaxhp;
static int         depth;

struct Mon { signed char x, y; int hp; unsigned char alive; };
static struct Mon    mon[MAX_MON];
static unsigned char nmon;

static unsigned char rcx[MAX_ROOMS], rcy[MAX_ROOMS], nrooms;
static unsigned char rx0[MAX_ROOMS], ry0[MAX_ROOMS], rw[MAX_ROOMS], rh[MAX_ROOMS];

static unsigned int  rngv;

/* turn message accumulator (events append; shown together) */
static char          msg[80];
static unsigned char mlen;
static unsigned char last_attr;   /* skip redundant vattr() during a repaint */

/* ---- RNG: 16-bit xorshift (7,9,8) ---- */
static unsigned int rnd16(void) {
    rngv ^= (unsigned int)(rngv << 7);
    rngv ^= (unsigned int)(rngv >> 9);
    rngv ^= (unsigned int)(rngv << 8);
    return rngv;
}
static unsigned char rndn(unsigned char n) { return (unsigned char)(rnd16() % n); }
static signed char sgn(int v) { return (v < 0) ? -1 : (v > 0 ? 1 : 0); }

static void msg_clear(void) { msg[0] = 0; mlen = 0; }
static void msg_add(const char *s) {
    if (mlen && mlen < 79) msg[mlen++] = ' ';
    while (*s && mlen < 79) msg[mlen++] = *s++;
    msg[mlen] = 0;
}
static void set_msg(const char *s) { msg_clear(); msg_add(s); }

/* ---- map generation: rooms + L-tunnels ---- */
static void dig_room(unsigned char x0, unsigned char y0, unsigned char w, unsigned char h) {
    unsigned char y, x;
    for (y = y0; y < y0 + h; y++)
        for (x = x0; x < x0 + w; x++) gmap[y][x] = T_FLOOR;
}
static void h_tun(unsigned char a, unsigned char b, unsigned char y) {
    unsigned char x, t;
    if (a > b) { t = a; a = b; b = t; }
    for (x = a; x <= b; x++) gmap[y][x] = T_FLOOR;
}
static void v_tun(unsigned char a, unsigned char b, unsigned char x) {
    unsigned char y, t;
    if (a > b) { t = a; a = b; b = t; }
    for (y = a; y <= b; y++) gmap[y][x] = T_FLOOR;
}

static void gen_level(void) {
    unsigned char i, x0, y0, w, h, cx, cy, pcx, pcy;

    for (i = 0; i < MAP_H; i++) {
        unsigned char x;
        for (x = 0; x < MAP_W; x++) {
            gmap[i][x] = T_WALL; expl[i][x] = 0; vis[i][x] = 0; occ[i][x] = 0;
        }
    }
    /* Place non-overlapping rooms (reject any that touch an existing one, so a
     * wall always separates them). This keeps "the room you're standing in" a
     * single rectangle -- which is what the room-lighting relies on. */
    nrooms = 0;
    {
        unsigned char attempts, j, ok;
        for (attempts = 0; attempts < 120 && nrooms < MAX_ROOMS; attempts++) {
            w  = 4 + rndn(8);
            h  = 3 + rndn(5);
            x0 = 1 + rndn((unsigned char)(MAP_W - w - 2));
            y0 = 1 + rndn((unsigned char)(MAP_H - h - 2));
            ok = 1;
            for (j = 0; j < nrooms; j++) {
                if (!(x0 + w < rx0[j] || x0 > rx0[j] + rw[j] ||
                      y0 + h < ry0[j] || y0 > ry0[j] + rh[j])) { ok = 0; break; }
            }
            if (!ok) continue;
            dig_room(x0, y0, w, h);
            cx = x0 + w / 2; cy = y0 + h / 2;
            if (nrooms > 0) {
                pcx = rcx[nrooms - 1]; pcy = rcy[nrooms - 1];
                if (rndn(2)) { h_tun(pcx, cx, pcy); v_tun(pcy, cy, cx); }
                else         { v_tun(pcy, cy, pcx); h_tun(pcx, cx, cy); }
            }
            rcx[nrooms] = cx; rcy[nrooms] = cy;
            rx0[nrooms] = x0; ry0[nrooms] = y0; rw[nrooms] = w; rh[nrooms] = h;
            nrooms++;
        }
    }
    px = rcx[0]; py = rcy[0];
    gmap[rcy[nrooms - 1]][rcx[nrooms - 1]] = T_STAIRS;

    nmon = 0;
    for (i = 1; i < nrooms && nmon < MAX_MON; i++) {
        if (rndn(3) != 0) {
            mon[nmon].x = rcx[i]; mon[nmon].y = rcy[i];
            mon[nmon].hp = 3 + depth; mon[nmon].alive = 1;
            occ[rcy[i]][rcx[i]] = nmon + 1;
            nmon++;
        }
    }
}

/* ---- field of view: recursive symmetric shadowcasting (Albert Ford's method).
 * Each of the 4 cardinal quadrants is scanned row by row from the player outward.
 * A row carries a [start,end] slope cone as integer fractions; a wall splits the
 * cone by recursing into the sub-cone above it. Divisions happen only per row (to
 * round the cone edges to columns), never per tile, and slope compares are pure
 * integer -- cheap enough for ~1 MHz, unlike a per-tile-divide caster. This is the
 * ONE model that gives real line-of-sight (a room opens fully when you stand in it,
 * but only a wedge is seen through a doorway) instead of ad-hoc room/corridor
 * lighting. `litx0..lity1` bounds the lit disc for the diff renderer + next clear. */
#define FOV_R 8
static unsigned char litx0, lity0, litx1, lity1;

static void clear_vis_box(void) {
    signed char y, x;
    for (y = lity0; y <= (signed char)lity1; y++) {
        unsigned char *vp = vis[y];
        for (x = litx0; x <= (signed char)litx1; x++) vp[x] = 0;
    }
}

/* floor / ceil integer division for a positive divisor b (C truncates toward 0) */
static int fdiv(int a, int b) { return (a >= 0) ? a / b : -(((-a) + b - 1) / b); }
static int cdiv(int a, int b) { return fdiv(a + b - 1, b); }

static unsigned char is_wall(signed char x, signed char y) {
    if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) return 1;   /* off-map blocks */
    return gmap[y][x] == T_WALL;
}
static void reveal(signed char x, signed char y) {
    int dx, dy;
    if (x < 0 || x >= MAP_W || y < 0 || y >= MAP_H) return;
    dx = x - px; dy = y - py;
    if (dx * dx + dy * dy <= FOV_R * FOV_R) { vis[y][x] = 1; expl[y][x] = 1; }
}

/* scan one row of quadrant q; start/end slopes are sN/sD and eN/eD (sD,eD > 0) */
static void scan(unsigned char q, int depth, int sN, int sD, int eN, int eD) {
    int minc, maxc, col, x, y;
    signed char prev = -2;      /* -2 none, 0 floor, 1 wall (prev tile in row) */
    unsigned char wall;
    if (depth > FOV_R) return;
    minc = fdiv(2 * depth * sN + sD, 2 * sD);   /* round_ties_up(depth * start) */
    maxc = cdiv(2 * depth * eN - eD, 2 * eD);   /* round_ties_down(depth * end) */
    for (col = minc; col <= maxc; col++) {
        switch (q) {                            /* map (depth,col) -> map cell */
            case 0:  x = px + col; y = py - depth; break;   /* north */
            case 1:  x = px + col; y = py + depth; break;   /* south */
            case 2:  x = px + depth; y = py + col; break;   /* east  */
            default: x = px - depth; y = py + col; break;   /* west  */
        }
        wall = is_wall((signed char)x, (signed char)y);
        reveal((signed char)x, (signed char)y);
        if (prev == 1 && !wall) { sN = 2 * col - 1; sD = 2 * depth; }  /* wall->floor */
        if (prev == 0 && wall)                                        /* floor->wall */
            scan(q, depth + 1, sN, sD, 2 * col - 1, 2 * depth);
        prev = (signed char)wall;
    }
    if (prev == 0) scan(q, depth + 1, sN, sD, eN, eD);    /* row ended on floor */
}

static void light(void) {
    unsigned char q;
    signed char a;

    clear_vis_box();                         /* unlight last frame's disc */
    reveal(px, py);
    for (q = 0; q < 4; q++) scan(q, 1, -1, 1, 1, 1);

    a = px - FOV_R; litx0 = (a < 0) ? 0 : a;         /* bounding box for render/clear */
    a = py - FOV_R; lity0 = (a < 0) ? 0 : a;
    a = px + FOV_R; litx1 = (a > MAP_W - 1) ? MAP_W - 1 : a;
    a = py + FOV_R; lity1 = (a > MAP_H - 1) ? MAP_H - 1 : a;
}

/* ---- rendering ---- */
static void put_cell(unsigned char g, unsigned char a) {
    if (a != last_attr) { vattr(a); last_attr = a; }
    vputc(g);
}
static void put_str(unsigned char x, unsigned char y, const char *s, unsigned char a) {
    vaddr((unsigned int)y * 80 + x);
    last_attr = 0xFF;
    while (*s) put_cell((unsigned char)*s++, a);
}
static void put_num(unsigned char x, unsigned char y, int v, unsigned char a) {
    char buf[6];
    signed char i = 0, j;
    if (v < 0) v = 0;
    if (v == 0) buf[i++] = '0';
    while (v > 0 && i < 5) { buf[i++] = (char)('0' + (v % 10)); v /= 10; }
    vaddr((unsigned int)y * 80 + x);
    last_attr = 0xFF;
    for (j = i - 1; j >= 0; j--) put_cell((unsigned char)buf[j], a);
}
static void clear_row(unsigned char y) {
    vattr(A_TEXT);
    vaddr((unsigned int)y * 80);
    vfill(' ');
    vcmd(VCMD_FILLROW);     /* one chip-side op instead of 80 cell writes */
    last_attr = 0xFF;
}

/* view codes: a 1-byte "appearance class" per cell so the renderer can diff */
#define V_BLANK  0
#define V_DFLOOR 1
#define V_DWALL  2
#define V_DSTAIR 3
#define V_FLOOR  4
#define V_WALL   5
#define V_STAIR  6
#define V_MON    7
#define V_PLAYER 8
static const unsigned char vglyph[9] = { ' ', '.', '#', '>', '.', '#', '>', 'r', '@' };
static const unsigned char vattrs[9] = { A_TEXT, A_DIM, A_DIM, A_DIM,
                                         A_FLOOR, A_WALL, A_STAIRS, A_MON, A_PLAYER };

/* Diff repaint. full=1 wipes the screen + resets the shadow buffer (new level);
 * otherwise we scan only the union of last frame's lit box and this one -- the
 * only cells whose appearance can have changed. The cell classification is inlined
 * with per-row pointers so there's no y*80 multiply per cell. Only cells whose view
 * code actually CHANGED are written. Status/message rows are redrawn only when
 * their contents change (so a plain step touches almost nothing). */
static int          shdepth = -1, shhp = -1, shmax = -1;   /* last status shown */
static char         shmsg[80];                             /* last message shown */

static void render(unsigned char full) {
    signed char x0, y0, x1, y1, y, x;
    static signed char pbx0, pby0, pbx1, pby1;   /* previous scan box */
    unsigned char code, t;

    if (full) {
        vattr(A_TEXT); vaddr(0); vfill(' '); vcmd(VCMD_CLEAR);
        for (y = 0; y < MAP_H; y++) {
            unsigned char *sh = shown[y];
            for (x = 0; x < MAP_W; x++) sh[x] = V_BLANK;   /* screen is now blank */
        }
        x0 = 0; y0 = 0; x1 = MAP_W - 1; y1 = MAP_H - 1;
    } else {
        x0 = litx0; if (pbx0 < x0) x0 = pbx0;    /* union(prev box, current lit box) */
        y0 = lity0; if (pby0 < y0) y0 = pby0;
        x1 = litx1; if (pbx1 > x1) x1 = pbx1;
        y1 = lity1; if (pby1 > y1) y1 = pby1;
    }
    last_attr = 0xFF;
    for (y = y0; y <= y1; y++) {
        unsigned char *gp = gmap[y], *vp = vis[y], *ep = expl[y];
        unsigned char *op = occ[y], *sh = shown[y];
        for (x = x0; x <= x1; x++) {
            if ((signed char)x == px && y == py)      code = V_PLAYER;
            else if (vp[x]) {
                if (op[x]) code = V_MON;
                else { t = gp[x]; code = (t == T_WALL) ? V_WALL : (t == T_STAIRS) ? V_STAIR : V_FLOOR; }
            } else if (ep[x]) {
                t = gp[x]; code = (t == T_WALL) ? V_DWALL : (t == T_STAIRS) ? V_DSTAIR : V_DFLOOR;
            } else code = V_BLANK;
            if (code != sh[x]) {
                vaddr((unsigned int)y * 80 + x);
                put_cell(vglyph[code], vattrs[code]);
                sh[x] = code;
            }
        }
    }
    pbx0 = litx0; pby0 = lity0; pbx1 = litx1; pby1 = lity1;

    if (full || depth != shdepth || php != shhp || pmaxhp != shmax) {
        clear_row(STA_ROW);
        put_str(0,  STA_ROW, "THE SUNLESS VAULT", A_STAIRS);
        put_str(20, STA_ROW, "DEPTH", A_TEXT); put_num(26, STA_ROW, depth, A_STAIRS);
        put_str(31, STA_ROW, "HP", A_TEXT);    put_num(34, STA_ROW, php, A_MON);
        put_str(38, STA_ROW, "/", A_TEXT);     put_num(39, STA_ROW, pmaxhp, A_TEXT);
        put_str(46, STA_ROW, "ARROWS  >DOWN  Q:QUIT", A_DIM);
        shdepth = depth; shhp = php; shmax = pmaxhp;
    }

    {
        unsigned char i = 0, diff = full;
        while (!diff) { if (msg[i] != shmsg[i]) diff = 1; else if (!msg[i]) break; i++; }
        if (diff) {
            clear_row(MSG_ROW);
            put_str(0, MSG_ROW, msg, A_TEXT);
            i = 0; do { shmsg[i] = msg[i]; } while (msg[i++]);
        }
    }
}

/* ---- creatures ---- */
static struct Mon *mon_at(signed char x, signed char y) {
    unsigned char i;
    for (i = 0; i < nmon; i++)
        if (mon[i].alive && mon[i].x == x && mon[i].y == y) return &mon[i];
    return 0;
}
/* returns 1 if the player actually moved (so FOV needs recomputing) */
static unsigned char try_move(signed char dx, signed char dy) {
    signed char nx = px + dx, ny = py + dy;
    struct Mon *m;
    if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) return 0;
    m = mon_at(nx, ny);
    if (m) {
        int dmg = rndn(4) + 2;
        m->hp -= dmg;
        if (m->hp <= 0) {
            m->alive = 0; occ[(unsigned char)m->y][(unsigned char)m->x] = 0;
            msg_add("You slay the creature!");
        } else msg_add("You strike the creature.");
        return 0;
    }
    if (gmap[ny][nx] == T_WALL) return 0;
    px = nx; py = ny;
    if (gmap[py][px] == T_STAIRS) msg_add("A stairway leads down (press >).");
    return 1;
}
static void mon_turn(void) {
    unsigned char i;
    struct Mon *m;
    for (i = 0; i < nmon; i++) {
        m = &mon[i];
        if (!m->alive) continue;
        if (!vis[(unsigned char)m->y][(unsigned char)m->x]) continue;
        {
            int adx = px - m->x, ady = py - m->y;
            signed char dx = sgn(adx), dy = sgn(ady);
            if (adx >= -1 && adx <= 1 && ady >= -1 && ady <= 1) {
                php -= (rndn(3) + 1);
                msg_add("The creature claws you!");
            } else {
                signed char nx = m->x + dx, ny = m->y + dy;
                signed char ox = m->x, oy = m->y;   /* old cell, for occ maintenance */
                if (gmap[ny][nx] != T_WALL && !mon_at(nx, ny)) { m->x = nx; m->y = ny; }
                else if (dx && gmap[m->y][m->x + dx] != T_WALL && !mon_at((signed char)(m->x + dx), m->y)) m->x += dx;
                else if (dy && gmap[m->y + dy][m->x] != T_WALL && !mon_at(m->x, (signed char)(m->y + dy))) m->y += dy;
                if (m->x != ox || m->y != oy) {     /* moved: keep the occupancy grid in sync */
                    occ[(unsigned char)oy][(unsigned char)ox] = 0;
                    occ[(unsigned char)m->y][(unsigned char)m->x] = i + 1;
                }
            }
        }
    }
}

/* ---- input: arrows (ESC[A/B/C/D) normalized to hjkl ----
 * Arrows arrive as 3 separate bytes (ESC, '[', letter). If the key buffer
 * overflows while a move key is held, individual bytes get dropped and the
 * sequence desyncs -- leaving a lone ESC. Because EVERY arrow starts with ESC,
 * a bare/partial ESC is ambiguous and must NOT be treated as an action (that's
 * why holding a key used to "crash" out to DOS: the stray ESC read as quit).
 * So a partial/unknown escape returns -1 (ignored); quitting is 'Q' only. */
static int readkey(void) {
    int c = INCH();
    if (c != 0x1B) return c;
    c = INCH_NB(); if (c != '[') return -1;
    c = INCH_NB();
    switch (c) {
        case 'A': return 'k';
        case 'B': return 'j';
        case 'C': return 'l';
        case 'D': return 'h';
    }
    return -1;
}

void main(void) {
    int k;
    unsigned char moved;
    vhidecur();
    rngv   = 0xACE1;
    depth  = 1;
    pmaxhp = 20; php = 20;
    set_msg("You enter the Sunless Vault...");
    gen_level();
    light();
    render(1);

    for (;;) {
        k = readkey();
        if (k == 'Q' || k == 'q') break;   /* ESC can't quit: arrows start with ESC */

        moved = 0;
        msg_clear();
        if      (k == 'h') moved = try_move(-1, 0);
        else if (k == 'l') moved = try_move(1, 0);
        else if (k == 'k') moved = try_move(0, -1);
        else if (k == 'j') moved = try_move(0, 1);
        else if (k == '.') moved = 1;                  /* wait: a turn passes */
        else if (k == '>') {
            if (gmap[py][px] == T_STAIRS) {
                depth++; set_msg("You descend deeper into the vault...");
                gen_level(); light(); render(1);
            } else { set_msg("There are no stairs here."); render(0); }
            continue;
        } else { continue; }

        mon_turn();
        if (php <= 0) { msg_add("You die in the dark. Press a key."); render(0); INCH(); break; }
        if (moved) light();
        render(0);
    }
    QUITDOS();
}
