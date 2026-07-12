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
unsigned char pweapon, parmor;
unsigned char pstr, pint, pcon, pdex, plevel;
char          pname[13];

static int    pxp, pxpnext;

unsigned char roll3d6(void) { return (unsigned char)(3 + rndn(6) + rndn(6) + rndn(6)); }
static int stat_hp(void)    { return 6 + pcon + (plevel - 1) * (4 + pcon / 6); }
static int stat_mana(void)  { return pint + (plevel - 1) * (pint / 3); }

void char_begin(void) {                  /* fresh level-1 hero from the rolled stats */
    plevel = 1; pxp = 0; pxpnext = 20; ppoison = 0; pgold = 0;
    pweapon = 0; parmor = 0;
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
    c->armor = parmor;                                    /* armor bonus soaks damage */
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
        m->hp -= r.dmg;
        if (m->hp <= 0) {
            m->alive = 0; occ[(unsigned char)m->y][(unsigned char)m->x] = 0;
            msg_add("You slay the"); msg_add(d.name);
            gain_xp(mondef[m->type].xp);
        } else { msg_add("You hit the"); msg_add(d.name); }
        return 0;
    }
    if (gmap[ny][nx] == T_WALL) return 0;
    px = nx; py = ny;
    if (gmap[py][px] == T_STAIRS) msg_add("A stairway leads down (press >).");
    return 1;
}
