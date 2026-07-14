/* ============================================================================
 * map.c -- the dungeon grid, procedural generation, and field of view.
 * Owns the terrain/visibility grids and the room table; monster spawning lives
 * in monster.c (generation only lays out terrain + stairs + the hero's start).
 * ==========================================================================*/
#include "vault.h"

unsigned char gmap[MAP_H][MAP_W];
unsigned char vseen[MAP_H][MAP_W];   /* bit0 seen, bit1 visible */
unsigned char ent[MAP_H][MAP_W];     /* monster (1..MAX_MON) or item (>MAX_MON), else 0 */
unsigned char rcx[MAX_ROOMS], rcy[MAX_ROOMS], nrooms;
unsigned char litx0, lity0, litx1, lity1;

static unsigned char rx0[MAX_ROOMS], ry0[MAX_ROOMS], rw[MAX_ROOMS], rh[MAX_ROOMS];

/* ---- generation: rooms + L-tunnels ---- */
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

void gen_level(void) {
    unsigned char i, x0, y0, w, h, cx, cy, pcx, pcy;

    for (i = 0; i < MAP_H; i++) {
        unsigned char x;
        for (x = 0; x < MAP_W; x++) {
            gmap[i][x] = T_WALL; vseen[i][x] = 0; ent[i][x] = 0;
        }
    }
    /* Place non-overlapping rooms (reject any that touch an existing one, so a
     * wall always separates them). */
    nrooms = 0;
    {
        unsigned char j, ok, attempts;
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

    /* At most one altar per floor (you can buy repeatedly at one, so more add no
     * value). Chance starts at 20% and climbs 20 points each altar-less floor,
     * resetting to 20% whenever one appears -- so a dry streak can't last long. */
    if (nrooms > 1) {
        static unsigned char shrine_chance = 20;
        if (rndn(100) < shrine_chance) {
            unsigned char tries;
            for (tries = 0; tries < 10; tries++) {
                unsigned char rr = 1 + rndn((unsigned char)(nrooms - 1));
                unsigned char sx = rx0[rr] + rndn(rw[rr]);
                unsigned char sy = ry0[rr] + rndn(rh[rr]);
                if (gmap[sy][sx] == T_FLOOR) { gmap[sy][sx] = T_SHRINE; break; }
            }
            shrine_chance = 20;
        } else {
            shrine_chance += 20;
            if (shrine_chance > 100) shrine_chance = 100;
        }
    }
}

/* a random walkable cell (for item scatter + teleport). Falls back to the
 * hero's start if the map is somehow too dense to find one quickly. */
void random_floor(signed char *ox, signed char *oy) {
    unsigned char tries;
    signed char x = px, y = py;
    for (tries = 0; tries < 200; tries++) {
        x = 1 + rndn(MAP_W - 2);
        y = 1 + rndn(MAP_H - 2);
        if (gmap[y][x] == T_FLOOR) break;
    }
    *ox = x; *oy = y;
}

/* ---- field of view: recursive symmetric shadowcasting (Albert Ford's method).
 * Each of 4 cardinal quadrants is scanned row by row outward; a row carries a
 * [start,end] slope cone as integer fractions, and a wall splits the cone by
 * recursing into the sub-cone above it. Divisions happen per row, not per tile,
 * so it's cheap at ~1 MHz while giving true line-of-sight. `litx0..lity1` bounds
 * the lit disc for the diff renderer + the next clear. */
static void clear_vis_box(void) {   /* clear the visible bit, preserve the seen bit */
    signed char y, x;
    for (y = lity0; y <= (signed char)lity1; y++) {
        unsigned char *vp = vseen[y];
        for (x = litx0; x <= (signed char)litx1; x++) vp[x] &= (unsigned char)~VVIS;
    }
}

/* current sight radius -- FOV_R normally, widened while the Light spell is up */
static unsigned char fov_r = FOV_R;

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
    if (dx * dx + dy * dy <= (int)fov_r * fov_r) vseen[y][x] |= (VVIS | VSEEN);
}

static void scan(unsigned char q, int d, int sN, int sD, int eN, int eD) {
    int minc, maxc, col, x, y;
    signed char prev = -2;      /* -2 none, 0 floor, 1 wall (prev tile in row) */
    unsigned char wall;
    if (d > fov_r) return;
    minc = fdiv(2 * d * sN + sD, 2 * sD);   /* round_ties_up(d * start) */
    maxc = cdiv(2 * d * eN - eD, 2 * eD);   /* round_ties_down(d * end) */
    for (col = minc; col <= maxc; col++) {
        switch (q) {                        /* map (d,col) -> map cell */
            case 0:  x = px + col; y = py - d; break;   /* north */
            case 1:  x = px + col; y = py + d; break;   /* south */
            case 2:  x = px + d; y = py + col; break;   /* east  */
            default: x = px - d; y = py + col; break;   /* west  */
        }
        wall = is_wall((signed char)x, (signed char)y);
        reveal((signed char)x, (signed char)y);
        if (prev == 1 && !wall) { sN = 2 * col - 1; sD = 2 * d; }   /* wall->floor */
        if (prev == 0 && wall)                                      /* floor->wall */
            scan(q, d + 1, sN, sD, 2 * col - 1, 2 * d);
        prev = (signed char)wall;
    }
    if (prev == 0) scan(q, d + 1, sN, sD, eN, eD);    /* row ended on floor */
}

void light(void) {
    unsigned char q;
    signed char a;

    fov_r = plight ? (FOV_R + 5) : FOV_R;    /* the Light spell widens the torch */

    clear_vis_box();                         /* unlight last frame's disc */
    reveal(px, py);
    for (q = 0; q < 4; q++) scan(q, 1, -1, 1, 1, 1);

    a = px - fov_r; litx0 = (a < 0) ? 0 : a;         /* bounding box for render/clear */
    a = py - fov_r; lity0 = (a < 0) ? 0 : a;
    a = px + fov_r; litx1 = (a > MAP_W - 1) ? MAP_W - 1 : a;
    a = py + fov_r; lity1 = (a > MAP_H - 1) ? MAP_H - 1 : a;
}
