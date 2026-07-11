/* ============================================================================
 * monster.c -- creatures: the live array, spawning, greedy AI, and their attack
 * (resolved through combat.c). Phase 2 step 2 replaces the single hard-coded
 * type here with a data-driven roster (see data.c).
 * ==========================================================================*/
#include "vault.h"

static struct Mon    mon[MAX_MON];
static unsigned char nmon;

struct Mon *mon_at(signed char x, signed char y) {
    unsigned char i;
    for (i = 0; i < nmon; i++)
        if (mon[i].alive && mon[i].x == x && mon[i].y == y) return &mon[i];
    return 0;
}

/* how a creature presents to the combat resolver (scales with depth for now) */
void mon_combatant(struct Mon *m, struct Combatant *c) {
    (void)m;
    c->name  = "the creature";
    c->acc   = (unsigned char)(6 + depth);
    c->eva   = (unsigned char)(2 + depth / 2);
    c->dmin  = 1;
    c->dmax  = (unsigned char)(3 + depth / 2);
    c->armor = 0;
}

void spawn_monsters(void) {
    unsigned char i;
    nmon = 0;
    for (i = 1; i < nrooms && nmon < MAX_MON; i++) {
        if (rndn(3) != 0) {              /* ~2/3 of rooms get a creature */
            mon[nmon].x = rcx[i]; mon[nmon].y = rcy[i];
            mon[nmon].hp = 3 + depth; mon[nmon].alive = 1;
            occ[rcy[i]][rcx[i]] = nmon + 1;
            nmon++;
        }
    }
}

void mon_turn(void) {
    unsigned char i;
    struct Mon *m;
    for (i = 0; i < nmon; i++) {
        m = &mon[i];
        if (!m->alive) continue;
        if (!vis[(unsigned char)m->y][(unsigned char)m->x]) continue;   /* only act when seen */
        {
            int adx = px - m->x, ady = py - m->y;
            signed char dx = sgn(adx), dy = sgn(ady);
            if (adx >= -1 && adx <= 1 && ady >= -1 && ady <= 1) {       /* adjacent: attack */
                struct Combatant a, d;
                struct AtkResult r;
                mon_combatant(m, &a);
                player_combatant(&d);
                resolve_attack(&a, &d, &r);
                if (!r.hit) msg_add("The creature misses.");
                else {
                    php -= r.dmg;
                    msg_add(r.crit ? "The creature savages you!" : "The creature claws you!");
                }
            } else {                                                    /* else step closer */
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
