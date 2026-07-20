/* ============================================================================
 * monster.c -- creatures: the live array, depth-based spawning from the data.c
 * roster, greedy AI, per-type combat (via combat.c), and the ability bits
 * (regeneration + poison for now). Adding a creature is a row in data.c.
 * ==========================================================================*/
#include "vault.h"

static struct Mon    mon[MAX_MON];
static unsigned char nmon;

/* the vampire currently draining the hero (or 0). While the drain link is live
 * (pdrain > 0) every point of damage the hero takes is fed to it as healing; it's
 * cleared the instant that vampire dies or the link times out / the level changes. */
static struct Mon   *drainer;

/* the 8 neighbour offsets, for summoning onto an adjacent free cell */
static const signed char DX[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
static const signed char DY[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };

/* Apply damage to the hero. All routes of player damage funnel through here so a
 * live vampiric drain link can siphon ALL of it (not just the triggering swing)
 * into the draining vampire, capped at its max HP. */
void player_take(int dmg) {
    php -= dmg;
    if (pdrain > 0 && drainer && drainer->alive) {
        int mx = (int)mondef[drainer->type].hp;
        drainer->hp += dmg;
        if (drainer->hp > mx) drainer->hp = mx;
    }
}

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
    score_dmg += dmg;
    if (m->hp <= 0) {
        m->alive = 0;
        if (m == drainer) { drainer = 0; pdrain = 0; }   /* its death ends the life drain at once */
        ent[(unsigned char)m->y][(unsigned char)m->x] = m->carry;   /* leave the item behind */
        gain_xp(mondef[m->type].xp);
        score_kills++;
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
    drainer = 0; pdrain = 0;             /* the old level's vampire is gone; drop any stale link */
    for (i = 1; i < nrooms && nmon < MAX_MON; i++) {
        if (depth == 15 && i == nrooms - 1) {    /* the Orb's room: the Guardian */
            ty = MON_GUARDIAN;
        } else {
            if (!porb && rndn(3) == 0) continue; /* ~1/3 empty normally; every room swarms on escape */
            ty = pick_type();
        }
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

/* raise a fresh skeleton on a free cell next to the necromancer (AB_SUMMON) */
static void mon_summon(struct Mon *nec) {
    unsigned char dir;
    if (nmon >= MAX_MON) return;               /* the live array is full */
    for (dir = 0; dir < 8; dir++) {
        signed char sx = (signed char)(nec->x + DX[dir]);
        signed char sy = (signed char)(nec->y + DY[dir]);
        if (sx < 0 || sy < 0 || sx >= MAP_W || sy >= MAP_H) continue;
        if (sx == px && sy == py) continue;
        if (!step_free(sx, sy)) continue;
        mon[nmon].type  = MON_SKELETON;
        mon[nmon].hp    = mondef[MON_SKELETON].hp;
        mon[nmon].x     = sx; mon[nmon].y = sy;
        mon[nmon].alive = 1;  mon[nmon].carry = 0;
        ent[(unsigned char)sy][(unsigned char)sx] = nmon + 1;
        nmon++;
        msg_add("The necromancer raises a skeleton!");
        return;
    }
}

void mon_turn(void) {
    unsigned char i, n = nmon;                 /* freeze the count: summons act next turn, not now */
    struct Mon *m;
    if (ptimestop) return;                     /* Time Stop: creatures are frozen */
    for (i = 0; i < n; i++) {
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
                /* the hydra flurries 1-3 blows; everything else strikes once */
                unsigned char hits = (d->abil & AB_MULTI) ? (unsigned char)(1 + rndn(3)) : 1;
                unsigned char h;
                for (h = 0; h < hits && php > 0; h++) {
                    struct Combatant a, def;
                    struct AtkResult r;
                    mon_combatant(m, &a);
                    player_combatant(&def);
                    resolve_attack(&a, &def, &r);
                    if (!r.hit) { msg_add("The"); msg_add(d->name); msg_add("misses."); continue; }
                    /* arm the drain link BEFORE the damage so this very swing feeds it too */
                    if (d->abil & AB_DRAIN) { pdrain = (int)(1 + rndn(3)); drainer = m; }
                    player_take(r.dmg);
                    msg_add("The"); msg_add(d->name);
                    msg_add(r.crit ? "savages you!" : "hits you.");
                    if (d->abil & AB_POISON) { ppoison += 4; msg_add("Poison!"); }
                    if (d->abil & AB_DRAIN)  msg_add("It drains your life!");
                    /* stun only when not already stunned AND past the immunity window, so a
                     * basilisk can't chain-lock you -- and neither can the message re-fire.
                     * pstunimm counts down every turn (incl. the stun turns): 2 stun + 1 free. */
                    if ((d->abil & AB_STUN) && pstun == 0 && pstunimm == 0 && rndn(3) == 0) {
                        pstun = 2; pstunimm = 4; msg_add("Its gaze paralyzes you!");
                    }
                    if ((d->abil & AB_CONFUSE) && pconfuse == 0 && rndn(3) == 0) {
                        pconfuse = 5; msg_add("Your mind reels -- you are confused!");
                    }
                }
            } else {                                                    /* else step closer */
                signed char ox = m->x, oy = m->y, cx = ox, cy = oy;
                if ((d->abil & AB_SUMMON) && rndn(5) == 0) mon_summon(m);  /* or raise the dead */
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
