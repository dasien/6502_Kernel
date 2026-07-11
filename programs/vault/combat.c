/* ============================================================================
 * combat.c -- actor-vs-actor resolution. Pure math on two Combatants: it does
 * NOT know whether the attacker is the hero, a beast, or (later) a spell/trap,
 * and it does NO I/O -- the caller reads the result, applies damage, and writes
 * its own log line. One place owns "did the attack hit, and for how much?".
 *
 *   hit%  = 70 + attacker.acc - defender.eva, clamped to [5, 95]
 *   dmg   = rand(dmin..dmax) - defender.armor, floored at 1 on a hit
 *   crit  = 10% chance, doubles the damage
 * ==========================================================================*/
#include "vault.h"

void resolve_attack(const struct Combatant *atk, const struct Combatant *def,
                    struct AtkResult *out) {
    int hitpct = 70 + (int)atk->acc - (int)def->eva;
    if (hitpct < 5)  hitpct = 5;
    if (hitpct > 95) hitpct = 95;

    out->hit = 0; out->crit = 0; out->dmg = 0;
    if ((int)rndn(100) >= hitpct) return;               /* a miss */

    out->hit = 1;
    out->dmg = atk->dmin + rndn((unsigned char)(atk->dmax - atk->dmin + 1));
    if (rndn(10) == 0) { out->dmg += out->dmg; out->crit = 1; }   /* 10% crit x2 */
    out->dmg -= def->armor;
    if (out->dmg < 1) out->dmg = 1;                     /* a landed blow always stings */
}
