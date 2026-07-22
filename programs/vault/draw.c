/* ============================================================================
 * draw.c -- all screen output: primitives, the diff-based map renderer, and the
 * status/message rows. The only module that talks to the VIC video port.
 * ==========================================================================*/
#include "vault.h"

static unsigned char shown[MAP_H][MAP_W];   /* last view-code drawn per cell (diff) */
static unsigned char last_attr;             /* skip redundant vattr() during a repaint */

void cls(void) {   /* clear the whole screen (shared by render + all the menus) */
    vattr(A_TEXT); vaddr(0); vfill(' '); vcmd(VCMD_CLEAR);
}
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
    signed char i = utoa(v, buf), j;
    vaddr((unsigned int)y * 80 + x);
    last_attr = 0xFF;
    for (j = i - 1; j >= 0; j--) put_cell((unsigned char)buf[j], a);
}
/* "a) text" -- the letter is always accented (A_STAIRS); the text takes the
 * caller's attr `a` (so a menu can dim un-affordable rows). */
void menu_row(unsigned char idx, unsigned char lcol, unsigned char tcol,
              unsigned char row, const char *text, unsigned char a) {
    char lbl[4];
    lbl[0] = (char)('a' + idx); lbl[1] = ')'; lbl[2] = ' '; lbl[3] = 0;
    put_str(lcol, row, lbl, A_STAIRS);
    put_str(tcol, row, text, a);
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
#define V_MON    7      /* unused for drawing now -- monster cells use VC_MON + type */
#define V_PLAYER 8
#define V_SHRINE 9
#define V_DSHRINE 10
#define V_ORB    11
#define V_DORB   12
#define VC_MON   16     /* monster view codes: VC_MON + roster index (so each type diffs) */
#define VC_ITEM  48     /* floor-item view codes: VC_ITEM + item index */
static const unsigned char vglyph[13] = { ' ', '.', '#', '>', '.', '#', '>', 'r', '@', 234, 234, 235, 235 };
static const unsigned char vattrs[13] = { A_TEXT, A_DIM, A_DIM, A_DIM,
                                          A_FLOOR, A_WALL, A_STAIRS, A_MON, A_PLAYER,
                                          A_WALL, A_DIM,      /* shrine lit / dim */
                                          A_PLAYER, A_DIM };  /* orb lit (bright yellow) / dim */

static int  shdepth = -1, shhp = -1, shmax = -1;   /* last status shown */
static int  shlevel = -1, shmana = -1, shgold = -1, shxp = -1;
static unsigned char shpsn = 2, shdrn = 2, shorb = 2;   /* last poisoned / drained / has-orb flags */

/* the single-value status fields, table-driven. STR/INT/CON/DEX moved to the
 * character/inventory page, freeing the middle of the row for the XP indicator. */
static const struct { unsigned char lcol, vcol, va; const char *lbl; } sfld[] = {
    { 13, 16, A_STAIRS, "LV" }, { 59, 62, A_STAIRS, "DL" },
};
#define NSFLD 2

/* the escape seal timer, "SEAL m:ss" (red under 30s), shown in place of gold while
 * carrying the Orb. Redrawn per turn AND once a second by the escape input loop. */
void draw_seal(void) {
    int s = seal_left, m; char b[6];
    if (s < 0) s = 0;
    m = s / 60;                                     /* fixed mm:ss so it never leaves stale digits */
    b[0] = (char)('0' + m / 10); b[1] = (char)('0' + m % 10); b[2] = ':';
    b[3] = (char)('0' + (s % 60) / 10); b[4] = (char)('0' + s % 10); b[5] = 0;
    put_str(65, STA_ROW, "SEAL", A_TEXT);
    put_str(70, STA_ROW, b, (s < 120) ? A_MON : A_STAIRS);   /* red in the last 2 minutes */
}

/* "XP n/next" progress toward the next level. Lives where the STR/INT/CON/DEX
 * fields used to be (those moved to the character/inventory page), so it has a
 * fixed home clear of the DL field and the escape seal clock. */
static void draw_xp(void) {
    char b[6];
    unsigned char c = 46;
    put_str(43, STA_ROW, "XP", A_TEXT);
    put_num(c, STA_ROW, pxp, A_STAIRS);
    c = (unsigned char)(c + utoa(pxp, b));       /* advance past the digits just drawn */
    put_str(c, STA_ROW, "/", A_TEXT);
    put_num((unsigned char)(c + 1), STA_ROW, pxpnext, A_STAIRS);
}

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
        cls();
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
        unsigned char *gp = gmap[y], *vsp = vseen[y], *entp = ent[y], *sh = shown[y];
        unsigned int rowbase = (unsigned int)y * 80;     /* hoisted out of the x loop */
        for (x = x0; x <= x1; x++) {
            if ((signed char)x == px && y == py)      code = V_PLAYER;
            else if (vsp[x] & VVIS) {
                unsigned char e = entp[x];
                if (e) { code = (e <= MAX_MON) ? (VC_MON + occ_type(e))       /* monster */
                                               : (VC_ITEM + iocc_type(e - MAX_MON)); }  /* item */
                else { t = gp[x]; code = (t == T_WALL) ? V_WALL : (t == T_STAIRS) ? V_STAIR :
                                                 (t == T_SHRINE) ? V_SHRINE : (t == T_ORB) ? V_ORB : V_FLOOR; }
            } else if (vsp[x] & VSEEN) {
                t = gp[x]; code = (t == T_WALL) ? V_DWALL : (t == T_STAIRS) ? V_DSTAIR :
                                          (t == T_SHRINE) ? V_DSHRINE : (t == T_ORB) ? V_DORB : V_DFLOOR;
            } else code = V_BLANK;
            if (code != sh[x]) {
                unsigned char g, at;
                if (code >= VC_ITEM)     { t = code - VC_ITEM; g = itemdef[t].glyph; at = itemdef[t].attr; }
                else if (code >= VC_MON) { t = code - VC_MON;  g = mondef[t].glyph;  at = mondef[t].attr; }
                else                     { g = vglyph[code];   at = vattrs[code]; }
                vaddr(rowbase + x);
                put_cell(g, at);
                sh[x] = code;
            }
        }
    }
    pbx0 = litx0; pby0 = lity0; pbx1 = litx1; pby1 = lity1;

    if (full || depth != shdepth || php != shhp || pmaxhp != shmax ||
        plevel != shlevel || pmana != shmana || pgold != shgold || pxp != shxp ||
        (ppoison > 0) != shpsn || (pdrain > 0) != shdrn || porb != shorb) {
        int sv[NSFLD];
        unsigned char i;
        sv[0] = plevel; sv[1] = depth;
        clear_row(STA_ROW);
        put_str(0,  STA_ROW, pname, A_STAIRS);
        put_str(19, STA_ROW, "HP", A_TEXT);  put_num(22, STA_ROW, php, A_MON);
        put_str(25, STA_ROW, "/", A_TEXT);   put_num(26, STA_ROW, pmaxhp, A_TEXT);
        put_str(31, STA_ROW, "MP", A_TEXT);  put_num(34, STA_ROW, pmana, A_STAIRS);
        put_str(37, STA_ROW, "/", A_TEXT);   put_num(38, STA_ROW, pmaxmana, A_TEXT);
        for (i = 0; i < NSFLD; i++) {
            put_str(sfld[i].lcol, STA_ROW, sfld[i].lbl, A_TEXT);
            put_num(sfld[i].vcol, STA_ROW, sv[i], sfld[i].va);
        }
        draw_xp();                                   /* XP progress in the freed middle slot */
        if (porb) draw_seal();                       /* escaping: seal clock here (gold lives on 'i') */
        if (ppoison > 0)     put_str(76, STA_ROW, "PSN", A_MON);   /* one DoT tag; poison wins if both */
        else if (pdrain > 0) put_str(76, STA_ROW, "DRN", A_MON);
        shdepth = depth; shhp = php; shmax = pmaxhp; shlevel = plevel; shmana = pmana;
        shgold = pgold; shxp = pxp; shpsn = (ppoison > 0); shdrn = (pdrain > 0); shorb = porb;
    }

    /* message row: repaint whenever there's a line (it changes almost every turn;
     * diffing it isn't worth an 80-byte shadow buffer). */
    clear_row(MSG_ROW);  put_str(0, MSG_ROW,  msg,  A_TEXT);   /* line 1: your actions */
    clear_row(MSG2_ROW); put_str(0, MSG2_ROW, msg2, A_TEXT);   /* line 2: what the world does */
}
