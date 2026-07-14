/* ============================================================================
 * spell.c -- the spellbook: the 'c' cast menu and the effects. Spells are
 * INT-gated (you know every spell your INT clears) and mana-costed. Content is
 * the SpellDef table in data.c; effects reach into the player/map/monster
 * modules through their public interfaces.
 * ==========================================================================*/
#include "vault.h"

/* apply spell `idx`. Returns 1 if it was actually cast (a turn passes), 0 if it
 * fizzled (no mana / no target) so the caller charges no turn. */
static unsigned char cast_spell(unsigned char idx) {
    const struct SpellDef *s = &spelldef[idx];

    if (s->kind == SP_TIMESTOP) {                 /* capstone: needs a minimum, eats all */
        if (pmana < s->cost) { msg_add("Not enough mana to still time."); return 0; }
        ptimestop = (unsigned char)(5 + pint / 4);
        pmana = 0;
        msg_add("Time shudders to a halt!");
        return 1;
    }

    if (pmana < s->cost) { msg_add("Not enough mana."); return 0; }

    switch (s->kind) {
        case SP_MISSILE: {
            struct Mon *m = nearest_vis_mon();
            if (!m) { msg_add("No target in sight."); return 0; }   /* no turn spent */
            msg_add("A bolt of force strikes the"); msg_add(mondef[m->type].name);
            if (mon_hurt(m, 3 + pint / 2 + rndn(4))) msg_add("-- it is destroyed!");
            break;
        }
        case SP_HEAL:
            php += 8 + pint; if (php > pmaxhp) php = pmaxhp;
            msg_add("Warmth knits your wounds.");
            break;
        case SP_LIGHT:
            plight = (unsigned char)(20 + pint);        /* torch: widen the sight radius */
            light();                                    /* recompute FOV at the bigger radius */
            msg_add("A radiance pushes back the dark.");
            break;
        case SP_SHIELD:
            pshield = (unsigned char)(8 + pint / 3);
            msg_add("A shield of force surrounds you.");
            break;
        case SP_TELE:
            random_floor(&px, &py); light();
            msg_add("The world folds -- you blink away.");
            break;
    }
    pmana -= s->cost;
    return 1;
}

/* the 'c' menu: lists spells your INT allows, with mana cost. Pick a letter to
 * cast. Returns 1 if a turn passed. */
unsigned char spell_screen(void) {
    int k;
    unsigned char i, n = 0, known[16];

    for (i = 0; i < nspelldef; i++)
        if (pint >= spelldef[i].minint) known[n++] = i;

    cls();
    put_str(34, 2, "SPELLS", A_STAIRS);
    put_str(30, 3, "Mana", A_TEXT); put_num(35, 3, pmana, A_STAIRS);
    put_str(38, 3, "/", A_TEXT);    put_num(39, 3, pmaxmana, A_TEXT);

    if (n == 0) {
        put_str(30, 5, "You know no spells (raise INT).", A_DIM);
    } else {
        for (i = 0; i < n; i++) {
            const struct SpellDef *s = &spelldef[known[i]];
            unsigned char row = (unsigned char)(5 + i);
            unsigned char a = (pmana >= s->cost) ? A_TEXT : A_DIM;   /* dim if unaffordable */
            menu_row(i, 28, 31, row, s->name, a);
            put_str(50, row, "mana", A_DIM); put_num(55, row, s->cost, a);
        }
    }
    put_str(22, (unsigned char)(7 + n), "[a-z] cast    [any other] back", A_DIM);

    k = INCH();
    if (k >= 'a' && k < 'a' + (int)n) return cast_spell(known[(unsigned char)(k - 'a')]);
    return 0;
}
