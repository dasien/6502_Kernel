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
int           seal_left;
int           score_dmg, score_heal, score_mana, score_kills, score_deep;
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
/* int -> ASCII digits into buf, LEAST significant first; returns the digit count.
 * Shared by put_num (screen) and msg_num (log), which both emit buf in reverse. */
signed char utoa(int v, char *buf) {
    signed char i = 0;
    if (v < 0) v = 0;
    if (v == 0) buf[i++] = '0';
    while (v > 0 && i < 5) { buf[i++] = (char)('0' + v % 10); v /= 10; }
    return i;
}
void msg_num(int v) {                    /* append " N" to the log */
    char b[6];
    signed char i = utoa(v, b), j;
    if (mlen && mlen < 79) msg[mlen++] = ' ';
    for (j = i - 1; j >= 0 && mlen < 79; j--) msg[mlen++] = b[j];
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

/* escape input: a NON-blocking wait that ticks the real-time seal clock off the
 * RTC while the player deliberates (so thinking costs time now), pausing while
 * Time Stop is up. Returns the next key (arrows -> hjkl) or -2 when time runs out.
 * Menus and level transitions run outside this loop, so their real seconds aren't
 * counted -- the pause falls out naturally (last-second is re-read on re-entry). */
static int escape_input(void) {
    static unsigned char last = 0xFF;           /* persists across turns (0xFF = resync) */
    for (;;) {
        int c;
        unsigned char s = rtc_sec();
        if (s != last) {                        /* a real second elapsed */
            if (last != 0xFF && !ptimestop && seal_left > 0) {   /* Time Stop freezes the seal */
                seal_left--;
                draw_seal();
                if (seal_left == 0) return -2;  /* the vault seals */
            }
            last = s;
        }
        c = INCH_NB();
        if (c == -1) continue;                  /* nothing yet: keep the clock ticking */
        if (c != 0x1B) return c;
        c = INCH_NB(); if (c != '[') return -1;
        c = INCH_NB();
        switch (c) { case 'A': return 'k'; case 'B': return 'j'; case 'C': return 'l'; case 'D': return 'h'; }
        return -1;
    }
}

/* ---- title / stat-roll / name entry ---- */
static void roll_screen(void) {
    int k;
    unsigned char n, accepted = 0;

    while (!accepted) {                       /* roll 3d6 per stat; reroll at will */
        pstr = roll3d6(); pint = roll3d6(); pcon = roll3d6(); pdex = roll3d6();
        cls();
        put_str(31, 3,  "THE SUNLESS VAULT", A_STAIRS);
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

    cls();
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

/* end-of-game screen: won = escaped to the surface with the Orb; else died.
 * Score rewards how deep you got AND how far you climbed back (round-trip depth),
 * plus kills, gold, and damage dealt; healing/mana are small tie-breakers. */
static void outcome_screen(unsigned char won) {
    int dep    = score_deep + (score_deep - depth);   /* deepest reached + levels climbed */
    int p_dep  = dep * 10;
    int p_kill = score_kills * 3;
    int p_dmg  = score_dmg / 2;
    int p_heal = score_heal / 4;
    int p_mana = score_mana / 4;
    int p_win  = won ? 500 : 0;
    int total  = p_dep + p_kill + p_dmg + p_heal + p_mana + pgold + p_win;
    unsigned char r;

    cls();
    if (won) {
        put_str(28, 2, "YOU CLIMB INTO THE DAWN", A_STAIRS);
        put_str(20, 4, "The Shimmering Orb blazes; light floods the world.", A_PLAYER);
        put_str(36, 6, "YOU WIN!", A_PLAYER);
    } else {
        put_str(33, 2, "YOU HAVE DIED", A_MON);
        put_str(25, 4, "The sunless dark closes over you forever.", A_DIM);
    }
    put_str(26, 9, "TALLY", A_DIM);  put_str(48, 9, "COUNT", A_DIM);  put_str(58, 9, "POINTS", A_DIM);
    r = 11;
    #define ROW(lbl, cnt, pts) do { put_str(26, r, (lbl), A_TEXT); \
        put_num(48, r, (cnt), A_STAIRS); put_num(58, r, (pts), A_PLAYER); r++; } while (0)
    ROW("Depth (round-trip)", dep,         p_dep);
    ROW("Creatures slain",    score_kills, p_kill);
    ROW("Damage dealt",       score_dmg,   p_dmg);
    ROW("Healing done",       score_heal,  p_heal);
    ROW("Mana spent",         score_mana,  p_mana);
    ROW("Gold gathered",      pgold,       pgold);
    if (won) ROW("Escaped with the Orb", 1, p_win);
    #undef ROW
    r++;
    put_str(26, r, "TOTAL SCORE", A_STAIRS); put_num(58, r, total, A_PLAYER);
    put_str(33, 23, "Press a key.", A_TEXT);
    flush_input(); INCH();
}

void main(void) {
    int k;
    unsigned char moved;
    vhidecur();
    rngv = rng_seed();                 /* seed from the RTC so each run differs */
    if (rngv == 0) rngv = 0xACE1;      /* xorshift must not start at zero */
    roll_screen();
    char_begin();
    depth = 1; score_deep = 1;
    score_dmg = score_heal = score_mana = score_kills = 0;
    set_msg("You enter the Sunless Vault.  (i)tems  (c)ast  >:descend  Q:quit");
    gen_level();
    spawn_monsters();
    spawn_items();
    light();
    render(1);

    for (;;) {
        k = porb ? escape_input() : readkey();
        flush_input();                     /* one turn per key press; drop the pile-up */
        if (k == -2) { outcome_screen(0); break; }   /* the seal clock ran out */
        if (k == 'Q' || k == 'q') break;   /* ESC can't quit: arrows start with ESC */

        if (k == 'W') {                    /* DEBUG: warp to L15, godlike, to test the endgame */
            depth = 15; score_deep = 15;
            debug_buff();
            gen_level(); spawn_monsters(); spawn_items(); light();
            set_msg("DEBUG: warped to L15, godlike. Slay the Guardian, take the Orb, climb out.");
            render(1);
            continue;
        }

        moved = 0;
        msg_clear();
        if      (k == 'h') moved = try_move(-1, 0);
        else if (k == 'l') moved = try_move(1, 0);
        else if (k == 'k') moved = try_move(0, -1);
        else if (k == 'j') moved = try_move(0, 1);
        else if (k == ' ' || k == '.') moved = 1;      /* wait: a turn passes */
        else if (k == 'i' || k == 'I') {               /* inventory (free unless used) */
            unsigned char used = inventory_screen();
            render(1);
            if (!used) continue;
            moved = 0;                                 /* item used: a turn passes, no move */
        }
        else if (k == 'c' || k == 'C') {               /* cast a spell (free unless cast) */
            unsigned char cast = spell_screen();
            render(1);
            if (!cast) continue;
            moved = 0;                                 /* spell cast: a turn passes */
        }
        else if (k == 'p' || k == 'P') {               /* pray at an altar */
            unsigned char used;
            if (gmap[py][px] != T_SHRINE) { set_msg("There is no altar here."); render(0); continue; }
            used = shrine_menu();
            render(1);
            if (!used) continue;
            moved = 0;                                 /* an offering: a turn passes */
        }
        else if (k == '>') {
            if (gmap[py][px] != T_STAIRS) { set_msg("There are no stairs here."); render(0); continue; }
            if (porb) {                                /* escaping: the stairs lead UP */
                if (--depth <= 0) { outcome_screen(1); break; }   /* reached the surface = win */
                set_msg("You climb toward the surface...");
            } else {
                depth++; if (depth > score_deep) score_deep = depth;
                set_msg("You descend deeper into the vault...");
            }
            gen_level(); spawn_monsters(); spawn_items(); light(); render(1);
            continue;
        } else { continue; }

        if (moved) try_pickup();                       /* grab whatever we stepped onto */
        player_tick();                                 /* effects + regen (poison, shield...) */
        mon_turn();                                    /* frozen if Time Stop is active */
        if (php <= 0) { render(0); outcome_screen(0); break; }
        if (moved) light();
        render(0);
    }
    flush_input();                         /* don't spill queued keys onto the DOS prompt */
    QUITDOS();
}
