/* ============================================================================
 * The Sunless Vault -- an MFC text roguelike (cc65).
 *
 * Original game: engine design after the author's Dungeon of Yacor; mechanics
 * inspired by Telengard / Sword of Fargoal. Talks to MFC only through glue.s.
 *
 * This file is the core + app shell: the RNG, the turn message log, shared
 * progress (depth), keyboard input, the title/roll/name screen, and the main
 * loop. The rest of the engine lives in cohesive modules (see vault.h): map.c,
 * draw.c, player.c, monster.c, combat.c.
 * ==========================================================================*/
#include "vault.h"

int           depth;
char          msg[80];
static unsigned char mlen;
static unsigned int  rngv;

/* ---- RNG: 16-bit xorshift (7,9,8) ---- */
unsigned int rnd16(void) {
    rngv ^= (unsigned int)(rngv << 7);
    rngv ^= (unsigned int)(rngv >> 9);
    rngv ^= (unsigned int)(rngv << 8);
    return rngv;
}
unsigned char rndn(unsigned char n) { return (unsigned char)(rnd16() % n); }
signed char   sgn(int v) { return (v < 0) ? -1 : (v > 0 ? 1 : 0); }

/* ---- turn message log (events append; drawn together by render) ---- */
void msg_clear(void) { msg[0] = 0; mlen = 0; }
void msg_add(const char *s) {
    if (mlen && mlen < 79) msg[mlen++] = ' ';
    while (*s && mlen < 79) msg[mlen++] = *s++;
    msg[mlen] = 0;
}
void set_msg(const char *s) { msg_clear(); msg_add(s); }

/* ---- input ----
 * Arrows arrive as 3 separate bytes (ESC, '[', letter). If the key buffer
 * overflows while a key is held, bytes get dropped and the sequence desyncs into
 * a lone ESC. Because EVERY arrow starts with ESC, a bare/partial ESC is
 * ambiguous and must NOT be an action -- so a partial escape returns -1
 * (ignored) and quitting is 'Q' only. */
static int readkey(void) {
    int c = INCH();
    if (c != 0x1B) return c;
    c = INCH_NB(); if (c != '[') return -1;
    c = INCH_NB();
    switch (c) {
        case 'A': return 'k';
        case 'B': return 'j';
        case 'C': return 'l';
        case 'D': return 'h';
    }
    return -1;
}
/* one turn per key press: discard input that piled up during a slow frame (or a
 * held key), so the backlog can't run you past danger or spill onto the prompt. */
static void flush_input(void) { while (INCH_NB() != -1) ; }

/* ---- title / stat-roll / name entry ---- */
static void roll_screen(void) {
    int k;
    unsigned char n, accepted = 0;

    while (!accepted) {                       /* roll 3d6 per stat; reroll at will */
        pstr = roll3d6(); pint = roll3d6(); pcon = roll3d6(); pdex = roll3d6();
        vattr(A_TEXT); vaddr(0); vfill(' '); vcmd(VCMD_CLEAR);
        put_str(31, 3,  "THE SUNLESS VAULT", A_STAIRS);
        put_str(16, 5,  "Descend fifteen floors and recover the Shimmering Orb.", A_DIM);
        put_str(34, 9,  "ROLL YOUR HERO", A_TEXT);
        put_str(34, 11, "STR", A_TEXT); put_num(40, 11, pstr, A_STAIRS);
        put_str(34, 12, "INT", A_TEXT); put_num(40, 12, pint, A_STAIRS);
        put_str(34, 13, "CON", A_TEXT); put_num(40, 13, pcon, A_STAIRS);
        put_str(34, 14, "DEX", A_TEXT); put_num(40, 14, pdex, A_STAIRS);
        put_str(25, 17, "[R] Reroll      [Enter] Accept", A_TEXT);
        for (;;) {
            k = INCH();
            if (k == 'r' || k == 'R') break;              /* reroll */
            if (k == 13 || k == 10) { accepted = 1; break; }
        }
    }

    vattr(A_TEXT); vaddr(0); vfill(' '); vcmd(VCMD_CLEAR);
    put_str(31, 8,  "THE SUNLESS VAULT", A_STAIRS);
    put_str(30, 11, "Name your hero:", A_TEXT);
    n = 0; pname[0] = 0;
    for (;;) {
        unsigned char i;
        vaddr((unsigned int)13 * 80 + 33);                /* draw the name field */
        for (i = 0; i < 12; i++) put_cell(i < n ? (unsigned char)pname[i] : '_', A_STAIRS);
        k = INCH();
        if (k == 13 || k == 10) break;
        if (k == 8 || k == 127) { if (n > 0) { n--; pname[n] = 0; } continue; }
        if (k >= 32 && k < 127 && n < 12) { pname[n++] = (char)k; pname[n] = 0; }
    }
    if (n == 0) { pname[0]='H'; pname[1]='E'; pname[2]='R'; pname[3]='O'; pname[4]=0; }
}

void main(void) {
    int k;
    unsigned char moved;
    vhidecur();
    rngv = rng_seed();                 /* seed from the RTC so each run differs */
    if (rngv == 0) rngv = 0xACE1;      /* xorshift must not start at zero */
    roll_screen();
    char_begin();
    depth = 1;
    set_msg("You enter the Sunless Vault...");
    gen_level();
    spawn_monsters();
    light();
    render(1);

    for (;;) {
        k = readkey();
        flush_input();                     /* one turn per key press; drop the pile-up */
        if (k == 'Q' || k == 'q') break;   /* ESC can't quit: arrows start with ESC */

        moved = 0;
        msg_clear();
        if      (k == 'h') moved = try_move(-1, 0);
        else if (k == 'l') moved = try_move(1, 0);
        else if (k == 'k') moved = try_move(0, -1);
        else if (k == 'j') moved = try_move(0, 1);
        else if (k == '.') moved = 1;                  /* wait: a turn passes */
        else if (k == '>') {
            if (gmap[py][px] == T_STAIRS) {
                depth++; set_msg("You descend deeper into the vault...");
                gen_level(); spawn_monsters(); light(); render(1);
            } else { set_msg("There are no stairs here."); render(0); }
            continue;
        } else { continue; }

        mon_turn();
        if (php <= 0) {
            msg_add("You die in the dark. Press a key.");
            render(0);
            flush_input(); INCH();         /* flush so the death screen isn't skipped */
            break;
        }
        if (moved) light();
        render(0);
    }
    flush_input();                         /* don't spill queued keys onto the DOS prompt */
    QUITDOS();
}
