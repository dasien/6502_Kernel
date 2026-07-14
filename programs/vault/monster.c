/* ============================================================================
 * monster.c -- creatures: the live array, depth-based spawning from the data.c
 * roster, greedy AI, per-type combat (via combat.c), and the ability bits
 * (regeneration + poison for now). Adding a creature is a row in data.c.
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
unsigned char occ_type(unsigned char oc) { return mon[oc - 1].type; }   /* for the renderer */

/* closest currently-visible live creature (squared distance), or 0 -- for spells */
struct Mon *nearest_vis_mon(void) {
    unsigned char i, best = 0xFF;
    int bestd = 30000;
    for (i = 0; i < nmon; i++) {
        int dx, dy, d;
        if (!mon[i].alive || !(vseen[(unsigned char)mon[i].y][(unsigned char)mon[i].x] & VVIS)) continue;
        dx = mon[i].x - px; dy = mon[i].y - py; d = dx * dx + dy * dy;
        if (d < bestd) { bestd = d; best = i; }
    }
    return (best == 0xFF) ? 0 : &mon[best];
}

/* a cell a monster may step into: not a wall, and not another monster (a floor
 * item -- ent > MAX_MON -- is fine; the mover will pick it up). */
static unsigned char step_free(signed char x, signed char y) {
    return gmap[y][x] != T_WALL && !(ent[y][x] && ent[y][x] <= MAX_MON);
}

/* apply damage; on death drop any carried item + award XP. Returns 1 if it died. */
unsigned char mon_hurt(struct Mon *m, int dmg) {
    m->hp -= dmg;
    if (m->hp <= 0) {
        m->alive = 0;
        ent[(unsigned char)m->y][(unsigned char)m->x] = m->carry;   /* leave the item behind */
        gain_xp(mondef[m->type].xp);
        return 1;
    }
    return 0;
}

/* how a creature presents to the combat resolver -- straight from its roster row */
void mon_combatant(struct Mon *m, struct Combatant *c) {
    const struct MonDef *d = &mondef[m->type];
    c->name  = d->name;
    c->acc   = d->acc;
    c->eva   = d->eva;
    c->dmin  = d->dmin;
    c->dmax  = d->dmax;
    c->armor = d->armor;
}

/* pick a roster type whose depth band includes the current floor */
static unsigned char pick_type(void) {
    unsigned char t, cand[16], nc = 0;
    for (t = 0; t < nmondef; t++)
        if (depth >= mondef[t].dlo && depth <= mondef[t].dhi) cand[nc++] = t;
    if (nc == 0) return 0;                       /* fallback: the rat */
    return cand[rndn(nc)];
}

void spawn_monsters(void) {
    unsigned char i, ty;
    nmon = 0;
    for (i = 1; i < nrooms && nmon < MAX_MON; i++) {
        if (rndn(3) != 0) {                      /* ~2/3 of rooms get a creature */
            ty = pick_type();
            mon[nmon].type  = ty;
            mon[nmon].hp    = mondef[ty].hp + rndn(3);
            mon[nmon].x     = rcx[i];
            mon[nmon].y     = rcy[i];
            mon[nmon].alive = 1;
            mon[nmon].carry = 0;
            ent[rcy[i]][rcx[i]] = nmon + 1;
            nmon++;
        }
    }
}

void mon_turn(void) {
    unsigned char i;
    struct Mon *m;
    if (ptimestop) return;                     /* Time Stop: creatures are frozen */
    for (i = 0; i < nmon; i++) {
        const struct MonDef *d;
        m = &mon[i];
        if (!m->alive) continue;
        if (!(vseen[(unsigned char)m->y][(unsigned char)m->x] & VVIS)) continue;   /* act only when seen */
        d = &mondef[m->type];
        if ((d->abil & AB_REGEN) && m->hp < (int)d->hp) m->hp++;        /* slow heal */
        {
            int adx = px - m->x, ady = py - m->y;
            signed char dx = sgn(adx), dy = sgn(ady);
            if (adx >= -1 && adx <= 1 && ady >= -1 && ady <= 1) {       /* adjacent: attack */
                struct Combatant a, def;
                struct AtkResult r;
                mon_combatant(m, &a);
                player_combatant(&def);
                resolve_attack(&a, &def, &r);
                if (!r.hit) { msg_add("The"); msg_add(d->name); msg_add("misses."); }
                else {
                    php -= r.dmg;
                    msg_add("The"); msg_add(d->name);
                    msg_add(r.crit ? "savages you!" : "hits you.");
                    if (d->abil & AB_POISON) { ppoison += 4; msg_add("Poison!"); }
                }
            } else {                                                    /* else step closer */
                signed char ox = m->x, oy = m->y, cx = ox, cy = oy;
                /* greedy step: diagonal if free, else along whichever axis is open.
                 * Items (ent > MAX_MON) don't block -- the mover walks over them. */
                if (step_free((signed char)(ox + dx), (signed char)(oy + dy))) { cx = ox + dx; cy = oy + dy; }
                else if (dx && step_free((signed char)(ox + dx), oy)) cx = ox + dx;
                else if (dy && step_free(ox, (signed char)(oy + dy))) cy = oy + dy;
                if (cx != ox || cy != oy) {
                    ent[(unsigned char)oy][(unsigned char)ox] = m->carry;    /* put back what we covered */
                    m->carry = (ent[(unsigned char)cy][(unsigned char)cx] > MAX_MON)
                                   ? ent[(unsigned char)cy][(unsigned char)cx] : 0;  /* cover any item here */
                    ent[(unsigned char)cy][(unsigned char)cx] = i + 1;
                    m->x = cx; m->y = cy;
                }
            }
        }
    }
}
