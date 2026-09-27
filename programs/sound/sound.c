/* ============================================================================
 * sound.c -- SOUND: what the SID can do.
 *
 * The machine's sound chip is register-faithful to the C64's SID: three voices,
 * each a triangle, sawtooth, pulse or noise oscillator with its own ADSR envelope,
 * and a resonant filter any of them can be routed through. This plays Grieg's "In
 * the Hall of the Mountain King" on all three -- a pulse lead with its width swept,
 * a sawtooth bass through the low-pass filter, and drums from the noise channel --
 * and speeds up each time round, as the piece does. Keys play each feature alone.
 *
 * The player is the way C64 music players worked: once a frame it moves the song
 * on, and every so many frames it starts the next row -- one eighth note -- on each
 * voice. A note is released just before the next one starts, so notes are
 * articulated rather than run together.
 *
 * SPACE music on/off   W waveforms   E envelopes   F filter   Q quit
 * ==========================================================================*/

extern int           INCH_NB(void);
extern void          QUITDOS(void);
extern void          vaddr(unsigned int cell);
extern void          vputc(unsigned char ch);
extern void          vattr(unsigned char a);
extern void          vfill(unsigned char ch);
extern void          vcmd(unsigned char cmd);
extern void          vhidecur(void);
extern void          wait_frame(void);

#define VCMD_CLEAR   0x01

/* The SID, at $FE38. Voice n's seven registers start at 7n. */
#define SID          ((volatile unsigned char *)0xFE38)
#define V_FREQ_LO    0
#define V_FREQ_HI    1
#define V_PW_LO      2
#define V_PW_HI      3
#define V_CTRL       4
#define V_AD         5
#define V_SR         6
#define FC_LO        (*(volatile unsigned char *)0xFE4D)
#define FC_HI        (*(volatile unsigned char *)0xFE4E)
#define RES_FILT     (*(volatile unsigned char *)0xFE4F)
#define MODE_VOL     (*(volatile unsigned char *)0xFE50)
#define SOUND_ENABLE (*(volatile unsigned char *)0x29)     /* the kernel's mute */

#define GATE      0x01
#define TRI       0x10
#define SAW       0x20
#define PULSE     0x40
#define NOISE     0x80
#define LP        0x10
#define BP        0x20
#define HP        0x40

/* SID frequency values for MIDI notes 33 (A1) to 96 (C7): FREQ = Hz * 2^24 / 1 MHz,
 * the chip's nominal clock, so these are the same numbers a C64 tune would use. */
#define NOTE_LO   33
static const unsigned int freq[] = {
      923u,   978u,  1036u,  1097u,  1163u,  1232u,  1305u,  1383u,
     1465u,  1552u,  1644u,  1742u,  1845u,  1955u,  2071u,  2195u,
     2325u,  2463u,  2610u,  2765u,  2930u,  3104u,  3288u,  3484u,
     3691u,  3910u,  4143u,  4389u,  4650u,  4927u,  5220u,  5530u,
     5859u,  6207u,  6577u,  6968u,  7382u,  7821u,  8286u,  8779u,
     9301u,  9854u, 10440u, 11060u, 11718u, 12415u, 13153u, 13935u,
    14764u, 15642u, 16572u, 17557u, 18601u, 19708u, 20879u, 22121u,
    23436u, 24830u, 26306u, 27871u, 29528u, 31284u, 33144u, 35115u,
};

/* ---- the tune --------------------------------------------------------------
 * One byte per voice per row, a row being an eighth note. 0 holds the note that is
 * sounding; REST releases it; anything else is a MIDI note. Drums use K, S and H.
 * Arranged in A minor: the theme, then again on the dominant, E. */
#define HOLD  0
#define REST  1
#define ROWS  64

static const unsigned char lead[ROWS] = {
    69,71,72,74, 76,72,76,HOLD,     /* A B C D  E C E-          */
    75,71,75,HOLD, 74,70,74,HOLD,   /* D# B D#-  D Bb D-        */
    69,71,72,74, 76,72,76,81,       /* A B C D  E C E A         */
    79,76,72,76, 79,HOLD,HOLD,REST, /* G E C E  G---            */
    76,78,80,81, 83,80,83,HOLD,     /* E F# G# A  B G# B-  (on E) */
    82,78,82,HOLD, 81,77,81,HOLD,   /* A# F# A#-  A F A-        */
    76,78,80,81, 83,80,83,88,       /* E F# G# A  B G# B E      */
    86,83,80,83, 86,HOLD,HOLD,REST, /* D B G# B  D---           */
};
static const unsigned char bass[ROWS] = {
    45,REST,45,REST, 45,REST,45,REST,
    47,REST,47,REST, 46,REST,46,REST,
    45,REST,45,REST, 45,REST,45,REST,
    48,REST,48,REST, 40,REST,40,REST,
    40,REST,40,REST, 40,REST,40,REST,
    42,REST,42,REST, 41,REST,41,REST,
    40,REST,40,REST, 40,REST,40,REST,
    44,REST,44,REST, 40,REST,40,REST,
};
#define K 'K'
#define S 'S'
#define H 'H'
static const unsigned char drums[8] = { K, H, S, H, K, H, S, H };

/* ---- the display ----------------------------------------------------------- */
#define KEY_LO    36            /* C2 */
#define KEY_HI    91            /* G6: one column a semitone, 56 of them */
#define KEY_COL   12
#define KEY_ROW   5
#define A_TITLE   0x4F
#define A_TEXT    0x07
#define A_DIM     0x40
#define A_LEAD    0x43          /* bright yellow */
#define A_BASS    0x46          /* bright cyan */
#define A_THIRD   0x45          /* bright magenta: the third voice in the demos */

static const unsigned char black_key[12] = { 0,1,0,1,0,0,1,0,1,0,1,0 };
static const char note_names[] = "C C#D D#E F F#G G#A A#B ";

static void say(unsigned char col, unsigned char row, unsigned char attr, const char *p)
{
    vattr(attr);
    vaddr((unsigned int)row * 80 + col);
    while (*p) vputc((unsigned char)*p++);
}

static void blank_line(unsigned char row)
{
    unsigned char i;
    vattr(A_TEXT);
    vaddr((unsigned int)row * 80);
    for (i = 0; i < 80; ++i) vputc(' ');
}

/* One key of the keyboard: two rows, the top showing black keys as grey, the bottom
 * white keys only. A lit key takes the colour of the voice playing it. */
static void draw_key(unsigned char note, unsigned char lit)
{
    unsigned char col = (unsigned char)(KEY_COL + note - KEY_LO);
    unsigned char blk = black_key[note % 12];
    vaddr((unsigned int)KEY_ROW * 80 + col);
    vattr(lit ? lit : (blk ? A_DIM : 0x07));
    vputc(219);
    vaddr((unsigned int)(KEY_ROW + 1) * 80 + col);
    vattr(blk ? 0x07 : (lit ? lit : 0x07));
    vputc(blk ? 223 : 219);     /* a black key ends halfway down */
}

static unsigned char lit_note[3];   /* the key each voice has lit, or 0 */

static void light(unsigned char voice, unsigned char note, unsigned char attr)
{
    if (lit_note[voice]) draw_key(lit_note[voice], 0);
    lit_note[voice] = 0;
    if (note >= KEY_LO && note <= KEY_HI) {
        draw_key(note, attr);
        lit_note[voice] = note;
    }
}

static void keyboard(void)
{
    unsigned char n;
    for (n = KEY_LO; n <= KEY_HI; ++n) draw_key(n, 0);
    say(KEY_COL, KEY_ROW + 2, A_DIM, "C2");
    say(KEY_COL + 24, KEY_ROW + 2, A_DIM, "C4");
    say(KEY_COL + 48, KEY_ROW + 2, A_DIM, "C6");
}

/* "VOICE 1  pulse     A4" -- what a voice is doing, beside its colour. */
static void voice_line(unsigned char voice, unsigned char attr, const char *role,
                       const char *wave, unsigned char note)
{
    static char name[4];
    unsigned char row = (unsigned char)(10 + voice);
    blank_line(row);
    say(2, row, attr, role);
    say(12, row, A_TEXT, wave);
    if (note >= NOTE_LO) {
        /* "A4", or "C#4" for a sharp: the name table pads naturals with a space. */
        unsigned char i = 0;
        name[i++] = note_names[(note % 12) * 2];
        if (note_names[(note % 12) * 2 + 1] != ' ') name[i++] = '#';
        name[i++] = (char)('0' + note / 12 - 1);
        name[i] = 0;
        say(24, row, attr, name);
    }
}

static void status(const char *msg)
{
    blank_line(15);
    say(2, 15, 0x4E, msg);
}

/* ---- the chip ---------------------------------------------------------------- */
static void set_note(unsigned char voice, unsigned char note)
{
    volatile unsigned char *v = SID + voice * 7;
    unsigned int f = freq[note - NOTE_LO];
    v[V_FREQ_LO] = (unsigned char)f;
    v[V_FREQ_HI] = (unsigned char)(f >> 8);
}

static void voice(unsigned char n, unsigned char ctrl, unsigned char ad, unsigned char sr)
{
    volatile unsigned char *v = SID + n * 7;
    v[V_AD] = ad;
    v[V_SR] = sr;
    v[V_CTRL] = ctrl;
}

static void pulse_width(unsigned char n, unsigned int pw)
{
    volatile unsigned char *v = SID + n * 7;
    v[V_PW_LO] = (unsigned char)pw;
    v[V_PW_HI] = (unsigned char)(pw >> 8);
}

static void cutoff(unsigned int fc)          /* 11 bits */
{
    FC_LO = (unsigned char)(fc & 7);
    FC_HI = (unsigned char)(fc >> 3);
}

static void silence(void)
{
    unsigned char n;
    for (n = 0; n < 3; ++n) SID[n * 7 + V_CTRL] = 0;
    for (n = 0; n < 3; ++n) light(n, 0, 0);
}

static void frames(unsigned char n)
{
    while (n--) wait_frame();
}

/* ---- the player -------------------------------------------------------------- */
static unsigned char row, tick, tempo, playing;
static unsigned int  pw = 0x800;
static signed char   pw_step = 24;

#define TEMPO_START 16          /* frames per eighth: about 112 beats a minute */
#define TEMPO_FAST  6           /* the frenzy at the end */

static void show_tempo(void)
{
    static char t[] = "tempo: 00 frames an eighth";
    t[7] = (char)('0' + tempo / 10);
    t[8] = (char)('0' + tempo % 10);
    say(50, 14, A_DIM, t);
}

static void music_setup(void)
{
    voice(0, PULSE, 0x08, 0xA4);    /* lead: quick attack, a little decay, held */
    voice(1, SAW,   0x05, 0x00);    /* bass: plucked -- no sustain */
    voice(2, NOISE, 0x04, 0x00);    /* drums: a short burst */
    RES_FILT = 0x42;                /* resonance 4, the BASS through the filter */
    cutoff(600);
    MODE_VOL = LP | 0x0F;           /* low-pass, full volume */
    row = 0; tick = 0; tempo = TEMPO_START;
    voice_line(0, A_LEAD, "LEAD", "pulse, swept", 0);
    voice_line(1, A_BASS, "BASS", "saw, low-pass", 0);
    voice_line(2, A_THIRD, "DRUMS", "noise", 0);
    show_tempo();
}

/* The next row: start or release each voice's note. */
static void play_row(void)
{
    unsigned char n = lead[row], d;

    if (n == REST) { SID[0 * 7 + V_CTRL] = PULSE; light(0, 0, 0); }
    else if (n != HOLD) {
        set_note(0, n);
        SID[0 * 7 + V_CTRL] = PULSE;          /* gate off, then on: a fresh attack */
        SID[0 * 7 + V_CTRL] = PULSE | GATE;
        light(0, n, A_LEAD);
        voice_line(0, A_LEAD, "LEAD", "pulse, swept", n);
    }

    n = bass[row];
    if (n == REST) { SID[1 * 7 + V_CTRL] = SAW; light(1, 0, 0); }
    else if (n != HOLD) {
        set_note(1, n);
        SID[1 * 7 + V_CTRL] = SAW;
        SID[1 * 7 + V_CTRL] = SAW | GATE;
        light(1, n, A_BASS);
        voice_line(1, A_BASS, "BASS", "saw, low-pass", n);
    }

    /* A drum is noise at a pitch: low and long for the kick, higher for the snare,
       highest and shortest for the hi-hat. */
    d = drums[row & 7];
    SID[2 * 7 + V_CTRL] = NOISE;
    if (d == K)      { SID[2*7+V_FREQ_HI] = 0x04; SID[2*7+V_AD] = 0x06; }
    else if (d == S) { SID[2*7+V_FREQ_HI] = 0x1C; SID[2*7+V_AD] = 0x05; }
    else             { SID[2*7+V_FREQ_HI] = 0x60; SID[2*7+V_AD] = 0x02; }
    SID[2 * 7 + V_CTRL] = NOISE | GATE;
    voice_line(2, A_THIRD, "DRUMS", d == K ? "noise: kick" : d == S ? "noise: snare"
                                                               : "noise: hi-hat", 0);
}

/* Once a frame: sweep the lead's pulse width, and every `tempo` frames, a row.
 * Two frames before a row ends, release the lead unless the next row holds it. */
static void music_frame(void)
{
    pw += pw_step;
    if (pw > 0xE00 || pw < 0x200) pw_step = (signed char)-pw_step;
    pulse_width(0, pw);

    if (tick == 0) play_row();
    if (tick == tempo - 2 && lead[(row + 1) & (ROWS - 1)] != HOLD)
        SID[0 * 7 + V_CTRL] = PULSE;

    if (++tick == tempo) {
        tick = 0;
        if (++row == ROWS) {        /* round again, and faster, as the piece does */
            row = 0;
            tempo = (unsigned char)(tempo > TEMPO_FAST ? tempo - 2 : TEMPO_START);
            show_tempo();
        }
    }
}

/* ---- the features, one at a time --------------------------------------------- */
static void demo_waveforms(void)
{
    static const unsigned char wave[5] = { TRI, SAW, PULSE, PULSE, NOISE };
    static const unsigned int  width[5] = { 0, 0, 0x800, 0x180, 0 };
    static const char *const   name[5] = {
        "TRIANGLE -- soft and hollow, like a flute",
        "SAWTOOTH -- bright and buzzy, every harmonic",
        "PULSE at 50% -- a square wave, the classic chip sound",
        "PULSE at 10% -- the same pitch, thin and nasal",
        "NOISE -- no pitch at all: drums, wind, explosions" };
    unsigned char i;
    MODE_VOL = 0x0F; RES_FILT = 0;
    for (i = 0; i < 5; ++i) {
        status(name[i]);
        voice_line(0, A_LEAD, "VOICE 1", i == 4 ? "noise" : i < 2 ? (i ? "sawtooth" : "triangle") : "pulse", 69);
        set_note(0, 69);            /* A4 */
        pulse_width(0, width[i]);
        voice(0, wave[i] | GATE, 0x00, 0xF0);
        light(0, 69, A_LEAD);
        frames(80);
        voice(0, wave[i], 0x00, 0xF4);
        frames(20);
    }
    light(0, 0, 0);
}

static void demo_envelopes(void)
{
    static const unsigned char arp[4] = { 60, 64, 67, 72 };    /* C E G C */
    unsigned char i;
    MODE_VOL = 0x0F; RES_FILT = 0;
    status("ENVELOPE: attack 0, decay 6, sustain 0 -- a pluck, like a harp");
    for (i = 0; i < 8; ++i) {
        set_note(0, arp[i & 3]);
        voice(0, SAW, 0x06, 0x00);
        voice(0, SAW | GATE, 0x06, 0x00);
        light(0, arp[i & 3], A_LEAD);
        frames(15);
    }
    light(0, 0, 0);
    status("ENVELOPE: attack 10, sustain 15, release 10 -- a pad that swells and fades");
    set_note(0, 60); set_note(1, 64); set_note(2, 67);
    for (i = 0; i < 3; ++i) {
        pulse_width(i, 0x800);
        voice(i, TRI | GATE, 0xA0, 0xFA);
        light(i, arp[i], i == 0 ? A_LEAD : i == 1 ? A_BASS : A_THIRD);
    }
    frames(150);
    status("...and released: the release setting decides how long it rings");
    for (i = 0; i < 3; ++i) SID[i * 7 + V_CTRL] = TRI;
    frames(120);
    silence();
}

static void demo_filter(void)
{
    static const unsigned char chord[3] = { 45, 52, 57 };      /* A2 E3 A3 */
    unsigned int fc;
    unsigned char i;
    for (i = 0; i < 3; ++i) {
        set_note(i, chord[i]);
        voice(i, SAW | GATE, 0x00, 0xF0);
        light(i, chord[i], i == 0 ? A_LEAD : i == 1 ? A_BASS : A_THIRD);
    }
    RES_FILT = 0xC7;                /* resonance 12, all three voices filtered */
    MODE_VOL = LP | 0x0F;
    status("LOW-PASS filter, cutoff sweeping up: the sound opens out");
    for (fc = 40; fc < 2000; fc += 16) { cutoff(fc); wait_frame(); }
    status("...and down again: the resonance whistles at the cutoff");
    for (fc = 2000; fc > 40; fc -= 16) { cutoff(fc); wait_frame(); }
    MODE_VOL = HP | 0x0F;
    status("HIGH-PASS: only what is above the cutoff gets through");
    for (fc = 40; fc < 1600; fc += 16) { cutoff(fc); wait_frame(); }
    for (i = 0; i < 3; ++i) SID[i * 7 + V_CTRL] = SAW;
    frames(30);
    silence();
    RES_FILT = 0;
}

/* ---- main --------------------------------------------------------------------- */
static void screen(void)
{
    vattr(A_TEXT);
    vfill(' ');
    vcmd(VCMD_CLEAR);
    vhidecur();
    say(0, 0, A_TITLE, " SOUND: the SID's three voices, envelopes and filter                            ");
    say(2, 2, A_TEXT, "Grieg, \"In the Hall of the Mountain King\", on all three voices.");
    keyboard();
    say(2, 18, 0x46, "SPACE");  say(8, 18, A_TEXT, "music on/off");
    say(2, 19, 0x46, "W");      say(8, 19, A_TEXT, "the four waveforms");
    say(2, 20, 0x46, "E");      say(8, 20, A_TEXT, "envelopes: a pluck and a pad");
    say(2, 21, 0x46, "F");      say(8, 21, A_TEXT, "the filter swept over a chord");
    say(2, 22, 0x46, "Q");      say(8, 22, A_TEXT, "quit");
}

void main(void)
{
    int key;

    screen();
    if (!SOUND_ENABLE)
        say(2, 3, 0x41, "System sound is muted, so this will be silent.");

    while (INCH_NB() >= 0) ;
    music_setup();
    playing = 1;
    status("playing -- it speeds up each time round");

    for (;;) {
        wait_frame();
        if (playing && SOUND_ENABLE) music_frame();

        key = INCH_NB();
        if (key < 0) continue;
        if (key == 'q' || key == 'Q') break;
        if (key == ' ') {
            playing ^= 1;
            if (playing) { music_setup(); status("playing -- it speeds up each time round"); }
            else         { silence(); status("stopped"); }
            continue;
        }
        if (!SOUND_ENABLE) continue;
        if (key == 'w' || key == 'W' || key == 'e' || key == 'E' || key == 'f' || key == 'F') {
            silence();
            if (key == 'w' || key == 'W') demo_waveforms();
            else if (key == 'e' || key == 'E') demo_envelopes();
            else demo_filter();
            while (INCH_NB() >= 0) ;        /* keys pressed meanwhile do not queue */
            if (playing) { music_setup(); status("playing -- it speeds up each time round"); }
            else status("stopped");
        }
    }

    silence();
    RES_FILT = 0;
    MODE_VOL = 0x0F;                /* no filter, full volume: as the kernel expects */
    vattr(0x02);
    vfill(' ');
    vcmd(VCMD_CLEAR);
    QUITDOS();
}
