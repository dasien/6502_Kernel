/* ============================================================================
 * data.c -- game content tables. Editing balance or adding a creature is a data
 * change here, not a code change. (Items and spells will join this file in
 * later steps.)
 *
 * MonDef columns:
 *   glyph, attr, name, hp, acc, eva, dmin, dmax, armor, xp, dlo, dhi, abil
 * depth range dlo..dhi controls where a type appears across the 15 floors, so
 * the descent escalates from rats to drakes.
 * ==========================================================================*/
#include "vault.h"

const struct MonDef mondef[] = {
    /* gly attr  name        hp  acc eva dmn dmx arm  xp  dlo dhi  abil */
    { 'r', 0x03, "rat",       4,  4,  3,  1,  2,  0,   2,  1,  3,  0 },
    { 'b', 0x07, "bat",       4,  6,  8,  1,  2,  0,   3,  1,  4,  0 },
    { 'k', 0x02, "kobold",    6,  5,  3,  1,  3,  0,   3,  1,  4,  0 },
    { 's', 0x42, "snake",     7,  6,  4,  1,  3,  0,   4,  2,  5,  AB_POISON },
    { 'g', 0x42, "goblin",   10,  7,  4,  2,  4,  0,   5,  2,  6,  0 },
    { 'o', 0x41, "orc",      15,  8,  3,  2,  5,  1,   7,  3,  8,  0 },
    { 'z', 0x05, "zombie",   20,  6,  1,  2,  5,  2,   8,  3,  8,  0 },
    { 'G', 0x03, "gnoll",    18,  9,  4,  3,  6,  1,   9,  4,  9,  0 },
    { 'O', 0x41, "ogre",     30,  9,  2,  4,  8,  2,  14,  5, 11,  0 },
    { 'w', 0x45, "wraith",   24, 11,  7,  3,  6,  1,  16,  6, 12,  AB_POISON },
    { 'T', 0x42, "troll",    42, 11,  3,  5,  9,  3,  22,  7, 13,  AB_REGEN },
    { 'W', 0x46, "wyvern",   34, 12,  6,  4,  8,  2,  20,  8, 14,  0 },
    { 'D', 0x41, "demon",    52, 14,  6,  6, 10,  3,  30, 10, 15,  0 },
    { 'Y', 0x45, "drake",    68, 15,  5,  8, 12,  4,  45, 12, 15,  0 },
};
const unsigned char nmondef = sizeof(mondef) / sizeof(mondef[0]);

/* ItemDef columns: glyph, attr, name, kind, magnitude, drop weight.
 * Glyphs are CP437 bytes: 173 (potion flask), 240 (scroll), 15 (coins).
 * Weights are relative -- gold is the common find, upgrades/map the rarest.
 * (Teleport is a spell, not a scroll; stat gains come from shrines.) */
const struct ItemDef itemdef[] = {
    {  15, 0x43, "gold",                IT_GOLD,   0, 30 },
    { 173, 0x41, "potion of healing",   IT_HEAL,  14, 18 },
    { 173, 0x46, "potion of mana",      IT_MANA,  10, 12 },
    { 240, 0x47, "scroll of magic map", IT_MAP,    0,  6 },
    { 140, 0x47, "weapon upgrade",      IT_WEAPON, 0,  5 },
    { 147, 0x46, "armor upgrade",       IT_ARMOR,  0,  5 },
};
const unsigned char nitemdef = sizeof(itemdef) / sizeof(itemdef[0]);

/* SpellDef columns: name, mana cost, min INT to know it, kind.
 * You "know" every spell your INT clears; mana limits how often you cast. */
const struct SpellDef spelldef[] = {
    { "Magic Missile",  4,  8, SP_MISSILE  },
    { "Heal",           8, 10, SP_HEAL     },
    { "Light",          3,  9, SP_LIGHT    },   /* torch: widens vision, not a map */
    { "Shield",        14, 11, SP_SHIELD   },
    { "Teleport",       2, 12, SP_TELE     },   /* cheap escape hatch */
    { "Time Stop",     20, 15, SP_TIMESTOP },   /* cost = min mana; consumes ALL */
};
const unsigned char nspelldef = sizeof(spelldef) / sizeof(spelldef[0]);
