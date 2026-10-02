/* ============================================================================
 * say.c -- SAY: S.A.M., the Software Automatic Mouth, on MFC.
 *
 * S.A.M. was written by Mark Barton and published by Don't Ask Software in 1982,
 * the first commercial speech synthesizer done entirely in software. The engine
 * is sam.s: the C64 version, as disassembled and documented by DLehenbauer
 * (vendor/c64-sam), ported to MFC's SID and clock. This file is only the front
 * end -- what to say, and how.
 *
 *   SAY HELLO THERE        say it, and return to the prompt
 *   SAY ]/HEH4LOW          the same, in SAM's own phonemes
 *   SAY                    a prompt: each line you type is spoken
 *
 * At the prompt, a line beginning with / is a setting rather than speech
 * (/HELP lists them), "." on its own says the last line again, and ESC on an
 * empty line -- or an empty line -- leaves.
 *
 * How it sounds: SAM holds SID voice 1 high and writes the master volume
 * thousands of times a second, a 4-bit sample each time. Its loops are its clock,
 * and were written for a 1 MHz 6502, so it switches MFC to 1 MHz while it speaks
 * (CPU_SPEED) and back after. It speaks with interrupts off, as on the C64, so
 * the jiffy counter stands still for the length of a sentence.
 * ==========================================================================*/

#include <string.h>

extern void          OUTCH(unsigned char c);    /* '\n' is a newline */
extern void          QUITDOS(void);
extern unsigned char read_line(void);           /* K_READ_LINE: length; text at $0200 */

extern unsigned char sam_input[256];            /* SAM's buffer, ended by $9B */
extern unsigned char sam_speed, sam_pitch, sam_throat, sam_mouth;
extern unsigned char sam_say_text(void);        /* $FF = said, else error position */
extern unsigned char sam_say_phonemes(void);
extern void          sam_set_mouth_throat(void);

#define CMDBUF        ((const char *)0x0200)
#define ARGBUF        ((const char *)0x0382)    /* DOS_ARGBUF: the command tail */
#define SOUND_ENABLE  (*(volatile unsigned char *)0x29)
#define SAM_EOL       0x9B
#define ESC           0x1B

/* RECITER writes its phonemes back over its input, so English has to leave room
 * for them to grow; phonemes are SAM's own and only need the terminator. */
#define TEXT_MAX      120
#define PHONEME_MAX   254

static void say_str(const char *s)
{
    while (*s) OUTCH((unsigned char)*s++);
}

static void say_num(unsigned char n)
{
    char buf[4];
    unsigned char i = 0;
    if (n >= 100) buf[i++] = (char)('0' + n / 100);
    if (n >= 10)  buf[i++] = (char)('0' + n / 10 % 10);
    buf[i++] = (char)('0' + n % 10);
    buf[i] = 0;
    say_str(buf);
}

static unsigned char upper(unsigned char c)
{
    return (c >= 'a' && c <= 'z') ? (unsigned char)(c - 'a' + 'A') : c;
}

/* Speak a line: English, or phonemes after a leading ']'. */
static void speak(const char *line, unsigned char len)
{
    unsigned char phon = 0, i, n, err;

    if (len && line[0] == ']') { phon = 1; line++; len--; }
    n = phon ? PHONEME_MAX : TEXT_MAX;
    if (len > n) {
        say_str(phon ? "Too long: SAM takes 254 phonemes at a time.\n"
                     : "Too long: up to 120 characters at a time.\n");
        return;
    }
    if (!SOUND_ENABLE) {
        say_str("System sound is muted.\n");
        return;
    }
    for (i = 0; i < len; i++) sam_input[i] = upper((unsigned char)line[i]);
    sam_input[len] = SAM_EOL;

    err = phon ? sam_say_phonemes() : sam_say_text();
    if (err == 0xFF) return;

    if (phon) {
        /* Show where SAM stopped reading, under the phonemes as typed. */
        say_str("SAM does not know this:\n ");
        for (i = 0; i < len; i++) OUTCH(upper((unsigned char)line[i]));
        say_str("\n ");
        for (i = 0; i < err && i < len; i++) OUTCH(' ');
        say_str("^\n");
    } else {
        say_str("SAM could not say that.\n");
    }
}

/* ---- settings ------------------------------------------------------------ */

static const struct { const char *name; unsigned char speed, pitch, throat, mouth; }
voices[] = {
    /* From S.A.M.'s manual. */
    { "SAM",    72, 64, 128, 128 },
    { "ELF",    72, 64, 110, 160 },
    { "ROBOT",  92, 60, 190, 190 },
    { "STUFFY", 82, 72, 110, 105 },
    { "LADY",   82, 32, 145, 145 },
    { "ET",    100, 64, 150, 200 },
};
#define NVOICES (sizeof voices / sizeof voices[0])

static void show(void)
{
    say_str("SPEED ");  say_num(sam_speed);
    say_str("  PITCH ");  say_num(sam_pitch);
    say_str("  THROAT "); say_num(sam_throat);
    say_str("  MOUTH ");  say_num(sam_mouth);
    OUTCH('\n');
}

static void help(void)
{
    unsigned char i;
    say_str("Type English to hear it, or ] then SAM's phonemes (]/HEH4LOW).\n"
            "  /SPEED n   1-255, higher is slower        (72)\n"
            "  /PITCH n   1-255, higher is lower         (64)\n"
            "  /THROAT n  /MOUTH n   the voice's shape   (128 128)\n"
            "  /VOICE name   ");
    for (i = 0; i < NVOICES; i++) { say_str(voices[i].name); OUTCH(' '); }
    say_str("\n  /SHOW   /HELP   /QUIT   . says the last line again\n");
}

/* A word of the line, matched in capitals: does `line` start with `word`, at a
 * word boundary? Returns the text after it, or 0. */
static const char *word(const char *line, const char *w)
{
    while (*w) {
        if (upper((unsigned char)*line) != (unsigned char)*w) return 0;
        line++; w++;
    }
    if (*line && *line != ' ') return 0;
    while (*line == ' ') line++;
    return line;
}

/* A number 1-255, or 0 if there is not one. */
static unsigned char number(const char *s)
{
    unsigned int v = 0;
    if (*s < '0' || *s > '9') return 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (unsigned int)(*s++ - '0'); if (v > 255) return 0; }
    return (unsigned char)v;
}

/* A '/' line. Returns 0 to quit. */
static unsigned char setting(const char *line)
{
    const char *a;
    unsigned char n, i;

    if ((a = word(line, "QUIT")) || (a = word(line, "Q"))) return 0;
    if ((a = word(line, "HELP")) || (a = word(line, "?"))) { help(); return 1; }
    if ((a = word(line, "SHOW"))) { show(); return 1; }
    if ((a = word(line, "VOICE"))) {
        for (i = 0; i < NVOICES; i++)
            if (word(a, voices[i].name)) {
                sam_speed = voices[i].speed; sam_pitch = voices[i].pitch;
                sam_throat = voices[i].throat; sam_mouth = voices[i].mouth;
                sam_set_mouth_throat();
                show();
                return 1;
            }
        say_str("Voices: ");
        for (i = 0; i < NVOICES; i++) { say_str(voices[i].name); OUTCH(' '); }
        OUTCH('\n');
        return 1;
    }
    if ((a = word(line, "SPEED")) || (a = word(line, "PITCH")) ||
        (a = word(line, "THROAT")) || (a = word(line, "MOUTH"))) {
        n = number(a);
        if (!n) { say_str("A number from 1 to 255.\n"); return 1; }
        switch (upper((unsigned char)line[0])) {
            case 'S': sam_speed = n; break;
            case 'P': sam_pitch = n; break;
            case 'T': sam_throat = n; sam_set_mouth_throat(); break;
            default:  sam_mouth = n;  sam_set_mouth_throat(); break;
        }
        show();
        return 1;
    }
    say_str("Not a setting. /HELP lists them.\n");
    return 1;
}

void main(void)
{
    unsigned char len;

    /* A command tail: say it and go back to the prompt. */
    if (ARGBUF[0]) {
        speak(ARGBUF, (unsigned char)strlen(ARGBUF));
        QUITDOS();
    }

    say_str("SAY -- S.A.M., the Software Automatic Mouth (Don't Ask Software, 1982)\n"
            "Type a line to hear it. /HELP for the voice, ESC or an empty line to leave.\n");
    for (;;) {
        OUTCH('>');
        OUTCH(' ');
        len = read_line();
        if (len == 0 || (len == 1 && CMDBUF[0] == ESC)) break;
        if (CMDBUF[0] == '/') {
            /* MON_CMDBUF is not terminated; setting() reads at most to a space,
               so end the line first. */
            ((char *)CMDBUF)[len < 80 ? len : 79] = 0;
            if (!setting(CMDBUF + 1)) break;
            continue;
        }
        speak(CMDBUF, len);
    }
    QUITDOS();
}
