/* ============================================================================
 * player.c -- the hero: rolled stats, HP/mana, XP + leveling, and movement
 * (which includes the bump-attack, resolved through combat.c). STR drives melee
 * damage, DEX drives accuracy/evasion, CON hit points, INT the mana pool.
 * ==========================================================================*/
#include "vault.h"

signed char   px, py;
int           php, pmaxhp, pmana, pmaxmana;
int           ppoison;
int           pgold;
unsigned char porb;
unsigned char pweapon, parmor;
unsigned char pshield, ptimestop, plight;
unsigned char pstr, pint, pcon, pdex, plevel;
char          pname[13];

static int    pxp, pxpnext;

unsigned char roll3d6(void) {          /* 3d6, floored at 8 so no hero is hopeless */
    unsigned char r = (unsigned char)(3 + rndn(6) + rndn(6) + rndn(6));
    return (r < 8) ? 8 : r;
}
static int stat_hp(void)    { return 6 + pcon + (plevel - 1) * (4 + pcon / 6); }
static int stat_mana(void)  { return 2 * pint + (plevel - 1) * (pint / 2); }

void char_begin(void) {                  /* fresh level-1 hero from the rolled stats */
    plevel = 1; pxp = 0; pxpnext = 20; ppoison = 0; pgold = 0; porb = 0;
    pweapon = 0; parmor = 0; pshield = 0; ptimestop = 0; plight = 0;
    pmaxhp = stat_hp();     php   = pmaxhp;
    pmaxmana = stat_mana(); pmana = pmaxmana;
}
/* DEBUG: a high-level, well-geared hero whose HP/mana come from the stat formula
 * (so a later level-up raises them, never resets them). */
void debug_buff(void) {
    pstr = pint = pcon = pdex = 18;
    pweapon = 5; parmor = 5;
    plevel = 20; pxp = 0; pxpnext = 20 * plevel * plevel;   /* ~8000 to next: won't level mid-test */
    pmaxhp = stat_hp();     php   = pmaxhp;
    pmaxmana = stat_mana(); pmana = pmaxmana;
}
void gain_xp(int amt) {
    pxp += amt;
    while (pxp >= pxpnext) {              /* level up: full heal + bigger pools */
        plevel++;
        pxpnext = 20 * plevel * plevel;
        pmaxhp = stat_hp();     php   = pmaxhp;
        pmaxmana = stat_mana(); pmana = pmaxmana;
        msg_add("You grow stronger!");
    }
}

/* how the hero presents to the combat resolver */
void player_combatant(struct Combatant *c) {
    c->name  = pname;
    c->acc   = (unsigned char)(8 + pdex / 2 + plevel);
    c->eva   = (unsigned char)(pdex / 2);
    c->dmin  = (unsigned char)(1 + pstr / 4 + pweapon);   /* STR + weapon bonus */
    c->dmax  = (unsigned char)(4 + pstr / 4 + pweapon);
    c->armor = (unsigned char)(parmor + (pshield ? 4 : 0)); /* armor + shield spell */
}

/* shrine boon: permanently raise a stat (and the pool it feeds) */
void player_bless(unsigned char which) {
    switch (which) {
        case 0: pstr++; break;
        case 1: pint++; pmaxmana = stat_mana(); break;
        case 2: pcon++; { int old = pmaxhp; pmaxhp = stat_hp(); php += pmaxhp - old; } break;
        case 3: pdex++; break;
    }
}

/* per-turn upkeep: tick temporary effects, bleed poison, and regenerate slowly */
void player_tick(void) {
    static unsigned char hreg, mreg;
    if (ppoison > 0)   { php--; ppoison--; msg_add("The poison gnaws."); }
    if (pshield > 0)   pshield--;
    if (ptimestop > 0) ptimestop--;
    if (plight > 0)    plight--;
    if (++hreg >= 15) { hreg = 0; if (php < pmaxhp && ppoison == 0) php++; }   /* heal / 15 turns */
    if (++mreg >= 25) { mreg = 0; if (pmana < pmaxmana) pmana++; }             /* mana / 25 turns */
}

/* returns 1 if the hero actually moved (so FOV needs recomputing) */
unsigned char try_move(signed char dx, signed char dy) {
    signed char nx = px + dx, ny = py + dy;
    struct Mon *m;
    if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) return 0;
    m = mon_at(nx, ny);
    if (m) {                             /* bump = melee attack */
        struct Combatant a, d;
        struct AtkResult r;
        player_combatant(&a);
        mon_combatant(m, &d);
        resolve_attack(&a, &d, &r);
        if (!r.hit) { msg_add("You miss the"); msg_add(d.name); return 0; }
        if (r.crit) msg_add("Critical hit!");
        if (mon_hurt(m, r.dmg)) { msg_add("You slay the"); msg_add(d.name); }
        else                    { msg_add("You hit the");  msg_add(d.name); }
        return 0;
    }
    if (gmap[ny][nx] == T_WALL) return 0;
    px = nx; py = ny;
    if (gmap[py][px] == T_STAIRS)
        msg_add(porb ? "A stairway leads up (press >)." : "A stairway leads down (press >).");
    else if (gmap[py][px] == T_SHRINE) msg_add("An altar stands here. (p)ray.");
    else if (gmap[py][px] == T_ORB) {              /* lift the Orb -> the escape begins */
        gmap[py][px] = T_STAIRS; porb = 1;         /* a way up opens where it lay */
        seal_left = SEAL_SECONDS;                  /* the real-time escape clock starts */
        msg_add("You lift the Shimmering Orb! The vault groans -- climb out (>)!");
    }
    return 1;
}
