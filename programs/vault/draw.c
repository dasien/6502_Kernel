/* ============================================================================
 * draw.c -- all screen output: primitives, the diff-based map renderer, and the
 * status/message rows. The only module that talks to the VIC video port.
 * ==========================================================================*/
#include "vault.h"

static unsigned char shown[MAP_H][MAP_W];   /* last view-code drawn per cell (diff) */
static unsigned char last_attr;             /* skip redundant vattr() during a repaint */

void put_cell(unsigned char g, unsigned char a) {
    if (a != last_attr) { vattr(a); last_attr = a; }
    vputc(g);
}
void put_str(unsigned char x, unsigned char y, const char *s, unsigned char a) {
    vaddr((unsigned int)y * 80 + x);
    last_attr = 0xFF;
    while (*s) put_cell((unsigned char)*s++, a);
}
void put_num(unsigned char x, unsigned char y, int v, unsigned char a) {
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

static int  shdepth = -1, shhp = -1, shmax = -1;   /* last status shown */
static int  shlevel = -1, shmana = -1;
static char shmsg[80];                             /* last message shown */

/* Diff repaint. full=1 wipes the screen + resets the shadow buffer (new level);
 * otherwise we scan only the union of last frame's lit box and this one. The cell
 * classification is inlined with per-row pointers so there's no y*80 multiply per
 * cell, and only cells whose view code CHANGED are written. Status/message rows
 * are redrawn only when their contents change. */
void render(unsigned char full) {
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
        unsigned char *gp = gmap[y], *vp = vis[y], *ep = seen[y];
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

    if (full || depth != shdepth || php != shhp || pmaxhp != shmax ||
        plevel != shlevel || pmana != shmana) {
        clear_row(STA_ROW);
        put_str(0,  STA_ROW, pname, A_STAIRS);
        put_str(13, STA_ROW, "LV", A_TEXT);  put_num(16, STA_ROW, plevel, A_STAIRS);
        put_str(19, STA_ROW, "HP", A_TEXT);  put_num(22, STA_ROW, php, A_MON);
        put_str(25, STA_ROW, "/", A_TEXT);   put_num(26, STA_ROW, pmaxhp, A_TEXT);
        put_str(31, STA_ROW, "MP", A_TEXT);  put_num(34, STA_ROW, pmana, A_STAIRS);
        put_str(37, STA_ROW, "/", A_TEXT);   put_num(38, STA_ROW, pmaxmana, A_TEXT);
        put_str(43, STA_ROW, "S", A_TEXT);   put_num(44, STA_ROW, pstr, A_FLOOR);
        put_str(47, STA_ROW, "I", A_TEXT);   put_num(48, STA_ROW, pint, A_FLOOR);
        put_str(51, STA_ROW, "C", A_TEXT);   put_num(52, STA_ROW, pcon, A_FLOOR);
        put_str(55, STA_ROW, "D", A_TEXT);   put_num(56, STA_ROW, pdex, A_FLOOR);
        put_str(61, STA_ROW, "DEPTH", A_TEXT); put_num(67, STA_ROW, depth, A_STAIRS);
        shdepth = depth; shhp = php; shmax = pmaxhp; shlevel = plevel; shmana = pmana;
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
