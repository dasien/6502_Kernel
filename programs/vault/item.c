/* ============================================================================
 * item.c -- floor items, the carried inventory, and item effects. Content lives
 * in data.c (the ItemDef table); this module scatters items on each level, picks
 * them up, shows the inventory menu, and applies effects (which reach into the
 * player/map globals -- item effects are inherently cross-cutting).
 * ==========================================================================*/
#include "vault.h"

struct Item { signed char x, y; unsigned char type, alive; };

static struct Item   fitem[MAX_FITEM];   /* items lying on the floor */
static unsigned char nfitem;
unsigned char        iocc[MAP_H][MAP_W];  /* floor-item index+1 at cell (for O(1) render) */

static unsigned char inv[MAX_INV];        /* carried item types */
static unsigned char ninv;

unsigned char iocc_type(unsigned char io) { return fitem[io - 1].type; }

void spawn_items(void) {
    unsigned char n, count, y, x;
    for (y = 0; y < MAP_H; y++) for (x = 0; x < MAP_W; x++) iocc[y][x] = 0;
    nfitem = 0;
    count = 2 + rndn(3);                  /* 2..4 items scattered per level */
    for (n = 0; n < count && nfitem < MAX_FITEM; n++) {
        signed char ix, iy;
        random_floor(&ix, &iy);
        if (gmap[iy][ix] != T_FLOOR) continue;
        if (occ[iy][ix] || iocc[iy][ix]) continue;
        if (ix == px && iy == py) continue;
        fitem[nfitem].x = ix; fitem[nfitem].y = iy;
        fitem[nfitem].type = rndn(nitemdef); fitem[nfitem].alive = 1;
        iocc[iy][ix] = nfitem + 1;
        nfitem++;
    }
}

void try_pickup(void) {
    unsigned char io = iocc[(unsigned char)py][(unsigned char)px];
    unsigned char ty;
    if (!io) return;
    ty = fitem[io - 1].type;

    if (itemdef[ty].kind == IT_GOLD) {              /* currency, not a pack item */
        int amt = 2 + rndn((unsigned char)(6 + depth * 3));
        pgold += amt;
        msg_add("You find"); msg_num(amt); msg_add("gold.");
    } else if (itemdef[ty].kind == IT_WEAPON) {     /* permanent attack buff, used at once */
        pweapon++;
        msg_add("Weapon upgraded! Now +"); msg_num(pweapon);
    } else if (itemdef[ty].kind == IT_ARMOR) {
        parmor++;
        msg_add("Armor upgraded! Now +"); msg_num(parmor);
    } else if (ninv >= MAX_INV) {
        msg_add("Your pack is full.");
        return;                                     /* leave it on the floor */
    } else {
        inv[ninv++] = ty;
        msg_add("You pick up the"); msg_add(itemdef[ty].name);
    }
    fitem[io - 1].alive = 0;
    iocc[(unsigned char)py][(unsigned char)px] = 0;
}

static void use_item(unsigned char slot) {
    unsigned char ty = inv[slot], i;
    switch (itemdef[ty].kind) {
        case IT_HEAL:
            php += itemdef[ty].mag; if (php > pmaxhp) php = pmaxhp;
            msg_add("You feel much better.");
            break;
        case IT_MANA:
            pmana += itemdef[ty].mag; if (pmana > pmaxmana) pmana = pmaxmana;
            msg_add("Magic surges within you.");
            break;
        case IT_STR:
            pstr++;
            msg_add("You feel mightier!");
            break;
        case IT_MAP: {
            unsigned char y, x;
            for (y = 0; y < MAP_H; y++) for (x = 0; x < MAP_W; x++) seen[y][x] = 1;
            msg_add("The level is laid bare.");
            break;
        }
        case IT_TELE:
            random_floor(&px, &py);
            light();
            msg_add("Reality lurches -- you blink away.");
            break;
    }
    for (i = slot; i + 1 < ninv; i++) inv[i] = inv[i + 1];   /* consume the item */
    ninv--;
}

/* the 'i' menu: list carried items, pick one to use. Returns 1 if a turn passed
 * (an item was used), 0 if it was only viewed. The caller repaints the map. */
unsigned char inventory_screen(void) {
    int k;
    unsigned char i;

    vattr(A_TEXT); vaddr(0); vfill(' '); vcmd(VCMD_CLEAR);
    put_str(34, 2, "INVENTORY", A_STAIRS);
    put_str(28, 3, "Weapon +", A_TEXT); put_num(36, 3, pweapon, A_FLOOR);
    put_str(40, 3, "Armor +", A_TEXT);  put_num(47, 3, parmor, A_FLOOR);
    if (ninv == 0) {
        put_str(34, 5, "(empty)", A_DIM);
    } else {
        char lbl[4];
        lbl[1] = ')'; lbl[2] = ' '; lbl[3] = 0;
        for (i = 0; i < ninv; i++) {
            lbl[0] = (char)('a' + i);
            put_str(28, (unsigned char)(5 + i), lbl, A_STAIRS);
            put_str(31, (unsigned char)(5 + i), itemdef[inv[i]].name, A_TEXT);
        }
    }
    put_str(22, (unsigned char)(7 + ninv), "[a-z] use    [any other] back", A_DIM);

    k = INCH();
    if (k >= 'a' && k < 'a' + (int)ninv) { use_item((unsigned char)(k - 'a')); return 1; }
    return 0;
}
