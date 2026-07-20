/* ============================================================================
 * item.c -- floor items, the carried inventory, and item effects. Content lives
 * in data.c (the ItemDef table); this module scatters items on each level, picks
 * them up, shows the inventory menu, and applies effects (which reach into the
 * player/map globals -- item effects are inherently cross-cutting).
 * ==========================================================================*/
#include "vault.h"

struct Item { signed char x, y; unsigned char type; };

static struct Item   fitem[MAX_FITEM];   /* items lying on the floor */
static unsigned char nfitem;

static unsigned char inv[MAX_INV];        /* carried item types */
static unsigned char ninv;

/* items live in the shared `ent` grid encoded as MAX_MON + index+1; the renderer
 * passes (ent - MAX_MON) here, i.e. the 1-based item index. */
unsigned char iocc_type(unsigned char io) { return fitem[io - 1].type; }

/* pick an item type by relative drop weight (gold common, upgrades/map rare) */
static unsigned char pick_item(void) {
    unsigned int total = 0, r;
    unsigned char i;
    for (i = 0; i < nitemdef; i++) total += itemdef[i].weight;
    r = rnd16() % total;
    for (i = 0; i < nitemdef; i++) {
        if (r < itemdef[i].weight) return i;
        r -= itemdef[i].weight;
    }
    return 0;
}

void spawn_items(void) {
    unsigned char n, count;
    /* the entity grid is already cleared by gen_level and populated with monsters;
     * items just fill empty floor cells. */
    nfitem = 0;
    count = 2 + rndn(3);                  /* 2..4 items scattered per level */
    for (n = 0; n < count && nfitem < MAX_FITEM; n++) {
        signed char ix, iy;
        random_floor(&ix, &iy);
        if (gmap[iy][ix] != T_FLOOR) continue;
        if (ent[iy][ix]) continue;                   /* occupied by a monster or item */
        if (ix == px && iy == py) continue;
        fitem[nfitem].x = ix; fitem[nfitem].y = iy;
        fitem[nfitem].type = pick_item();
        ent[iy][ix] = (unsigned char)(MAX_MON + nfitem + 1);
        nfitem++;
    }
}

void try_pickup(void) {
    unsigned char e = ent[(unsigned char)py][(unsigned char)px];
    unsigned char ty;
    if (e <= MAX_MON) return;                        /* 0 = nothing, <=MAX_MON = monster */
    ty = fitem[e - MAX_MON - 1].type;

    if (itemdef[ty].kind == IT_GOLD) {              /* currency, not a pack item */
        int amt = 10 + depth * 3 + rndn((unsigned char)(10 + depth * 4));
        pgold += amt;                 /* spendable purse */
        score_gold += amt;            /* gross collected, for the end-screen tally */
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
    ent[(unsigned char)py][(unsigned char)px] = 0;
}

static void use_item(unsigned char slot) {
    unsigned char ty = inv[slot], i;
    switch (itemdef[ty].kind) {
        case IT_HEAL: {
            int old = php;
            php += itemdef[ty].mag; if (php > pmaxhp) php = pmaxhp;
            score_heal += php - old;
            msg_add("You feel much better.");
            break;
        }
        case IT_MANA:
            pmana += itemdef[ty].mag; if (pmana > pmaxmana) pmana = pmaxmana;
            msg_add("Magic surges within you.");
            break;
        case IT_MAP: {
            unsigned char y, x;
            for (y = 0; y < MAP_H; y++) for (x = 0; x < MAP_W; x++) vseen[y][x] |= VSEEN;
            msg_add("The level is laid bare.");
            break;
        }
    }
    for (i = slot; i + 1 < ninv; i++) inv[i] = inv[i + 1];   /* consume the item */
    ninv--;
}

/* the 'p' shrine menu: donate gold for restoration or a permanent blessing. Both
 * services grow more expensive the more you buy. Returns 1 if a turn passed. */
unsigned char shrine_menu(void) {
    static unsigned char nrest, nbless;      /* purchases so far -> rising prices */
    int rcost = 15 + 15 * nrest;
    int bcost = 30 + 30 * nbless;
    int k;
    static const char *bn[4] = { "Blessing of Might  (STR)", "Blessing of Mind   (INT)",
                                 "Blessing of Vigor  (CON)", "Blessing of Grace  (DEX)" };
    unsigned char i;

    cls();
    put_str(26, 2, "AN ALTAR TO THE DROWNED GODS", A_STAIRS);
    put_str(30, 4, "Gold", A_TEXT); put_num(35, 4, pgold, A_PLAYER);

    menu_row(0, 24, 27, 6, "Restore body and mind", A_TEXT);
    put_str(54, 6, "gold", A_DIM); put_num(59, 6, rcost, A_TEXT);
    for (i = 0; i < 4; i++) {
        menu_row((unsigned char)(1 + i), 24, 27, (unsigned char)(8 + i), bn[i], A_TEXT);
        put_str(54, (unsigned char)(8 + i), "gold", A_DIM);
        put_num(59, (unsigned char)(8 + i), bcost, A_TEXT);
    }
    put_str(24, 14, "[a-e] offer     [any other] leave", A_DIM);

    k = INCH();
    if (k == 'a') {
        if (pgold < rcost) { msg_add("Not enough gold."); return 0; }
        pgold -= rcost; nrest++;
        score_heal += pmaxhp - php;
        php = pmaxhp; pmana = pmaxmana;
        msg_add("The gods restore your body and mind.");
        return 1;
    }
    if (k >= 'b' && k <= 'e') {
        if (pgold < bcost) { msg_add("Not enough gold."); return 0; }
        pgold -= bcost; nbless++;
        player_bless((unsigned char)(k - 'b'));
        msg_add("A cold blessing settles into your bones.");
        return 1;
    }
    return 0;
}

/* the 'i' menu: list carried items, pick one to use. Returns 1 if a turn passed
 * (an item was used), 0 if it was only viewed. The caller repaints the map. */
unsigned char inventory_screen(void) {
    int k;
    unsigned char i;

    cls();
    put_str(34, 2, "INVENTORY", A_STAIRS);
    put_str(24, 3, "Weapon +", A_TEXT); put_num(32, 3, pweapon, A_FLOOR);
    put_str(36, 3, "Armor +", A_TEXT);  put_num(43, 3, parmor, A_FLOOR);
    put_str(47, 3, "Gold", A_TEXT);     put_num(52, 3, pgold, A_PLAYER);
    if (ninv == 0) {
        put_str(34, 5, "(empty)", A_DIM);
    } else {
        for (i = 0; i < ninv; i++)
            menu_row(i, 28, 31, (unsigned char)(5 + i), itemdef[inv[i]].name, A_TEXT);
    }
    put_str(22, (unsigned char)(7 + ninv), "[a-z] use    [any other] back", A_DIM);

    k = INCH();
    if (k >= 'a' && k < 'a' + (int)ninv) { use_item((unsigned char)(k - 'a')); return 1; }
    return 0;
}
