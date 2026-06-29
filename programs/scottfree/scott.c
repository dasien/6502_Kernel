/*
 *  ScottFree on MFC-DOS -- the runtime engine.
 *
 *  Ported from ScottFree 1.14 (Alan Cox / Swansea University Computer
 *  Society, GNU GPL). The interpreter logic (PerformLine/PerformActions and
 *  the parser) is preserved; the database loader is gone (the game tables are
 *  pre-parsed by dat2c and linked in), and curses I/O is replaced by the
 *  MFC-DOS character console via glue.s.
 *
 *  cc65 portability notes:
 *    - int is 16-bit, so every BitFlags shift uses 1L<< (flags exceed bit 15).
 *    - build with --signed-chars.
 */
#include <string.h>
#include "scott.h"

/* ---- console + RNG provided by glue.s ---- */
void OUTCH(char c);
char INCH(void);
void CLS(void);
int  RND(void);
void QUITDOS(void);             /* return to the DOS ] prompt */

/* ---- DOS file I/O (saves live on the mounted disk) ---- */
char dopen_read(char *name);    /* 0 = ok, 1 = error */
char dopen_write(char *name);   /* 0 = ok, 1 = error */
int  dgetb(void);               /* next byte 0..255, or -1 at EOF */
char dputb(char c);             /* 0 = ok, 1 = error */
void dclose(void);
char dir_first(char *dst11);    /* enumerate dir: 0 = entry in dst, 1 = none */
char dir_next(char *dst11);

void SaveGame(void);            /* defined below; called from GetInput */
void RestoreGame(void);
void Look(void);

#define W while
#define MyLoc (GameHeader.PlayerRoom)

int  LightRefill;
char NounText[16];
int  Counters[16];
int  CurrentCounter;
int  SavedRoom;
int  RoomSaved[16];
int  Options;
int  Redraw;
int  Width = 80;                /* MFC-DOS screen is 80 columns */
int  OutputPos = 0;
long BitFlags = 0;

/* ------------------------------------------------------------------ */
/* small libc helpers cc65 doesn't ship                                */
/* ------------------------------------------------------------------ */
static char up(char c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
/* strcasecmp / strncasecmp come from cc65's <string.h>. */

/* ------------------------------------------------------------------ */
/* output: word-wrap to the 80-column console                          */
/* ------------------------------------------------------------------ */
static void Newline(void) { OUTCH('\n'); OutputPos = 0; }

void OutBuf(char *buffer)
{
    char word[64];
    int wp;
    W (*buffer) {
        if (OutputPos == 0)
            W (*buffer && (*buffer == ' ' || *buffer == '\t' || *buffer == '\n')) {
                if (*buffer == '\n') Newline();
                buffer++;
            }
        if (*buffer == 0) return;
        wp = 0;
        W (*buffer && *buffer != ' ' && *buffer != '\t' && *buffer != '\n')
            if (wp < 63) word[wp++] = *buffer++; else buffer++;
        word[wp] = 0;
        if (OutputPos + wp > Width - 2) Newline();
        { char *w = word; W (*w) OUTCH(*w++); }
        OutputPos += wp;
        if (*buffer == 0) return;
        if (*buffer == '\n') Newline();
        else { OutputPos++; if (OutputPos < Width - 1) OUTCH(' '); }
        buffer++;
    }
}

void Output(char *a) { OutBuf(a); }

void OutputNumber(int a)
{
    char buf[8]; int i = 0; unsigned v;
    if (a < 0) { OUTCH('-'); v = -a; } else v = a;
    do { buf[i++] = '0' + v % 10; v /= 10; } W (v);
    W (i) OUTCH(buf[--i]);
    OUTCH(' '); OutputPos += 1;
}

/* ------------------------------------------------------------------ */
int CountCarried(void)
{
    int ct = 0, n = 0;
    W (ct <= GameHeader.NumItems) { if (Items[ct].Location == CARRIED) n++; ct++; }
    return n;
}

char *MapSynonym(char *word)
{
    int n = 1; char *tp;
    static char lastword[16];
    W (n <= GameHeader.NumWords) {
        tp = Nouns[n];
        if (*tp == '*') tp++; else { int k = 0; W (tp[k] && k < 15) { lastword[k] = tp[k]; k++; } lastword[k] = 0; }
        if (strncasecmp(word, tp, GameHeader.WordLength) == 0) return lastword;
        n++;
    }
    return (char *)0;
}

int MatchUpItem(char *text, int loc)
{
    char *word = MapSynonym(text);
    int ct = 0;
    if (word == (char *)0) word = text;
    W (ct <= GameHeader.NumItems) {
        if (Items[ct].AutoGet && Items[ct].Location == loc &&
            strncasecmp(Items[ct].AutoGet, word, GameHeader.WordLength) == 0)
            return ct;
        ct++;
    }
    return -1;
}

int WhichWord(char *word, char **list)
{
    int n = 1, ne = 1; char *tp;
    W (ne <= GameHeader.NumWords) {
        tp = list[ne];
        if (*tp == '*') tp++; else n = ne;
        if (strncasecmp(word, tp, GameHeader.WordLength) == 0) return n;
        ne++;
    }
    return -1;
}

int RandomPercent(int n)
{
    unsigned rv = (unsigned)RND();
    rv %= 100;
    return rv < (unsigned)n;
}

/* ------------------------------------------------------------------ */
void Look(void)
{
    static char *ExitNames[6] = { "North", "South", "East", "West", "Up", "Down" };
    Room *r; int ct, f;

    if ((BitFlags & (1L << DARKBIT)) && Items[LIGHT_SOURCE].Location != CARRIED
        && Items[LIGHT_SOURCE].Location != MyLoc) {
        Output(Options & YOUARE ? "You can't see. It is too dark!\n"
                                : "I can't see. It is too dark!\n");
        return;
    }
    r = &Rooms[MyLoc];
    if (*r->Text == '*') { Output(r->Text + 1); Output("\n"); }
    else { Output(Options & YOUARE ? "You are " : "I'm in a "); Output(r->Text); Output("\n"); }

    Output("\nObvious exits: ");
    ct = 0; f = 0;
    W (ct < 6) {
        if (r->Exits[ct] != 0) { if (f) Output(", "); else f = 1; Output(ExitNames[ct]); }
        ct++;
    }
    if (f == 0) Output("none");
    Output(".\n");

    ct = 0; f = 0;
    W (ct <= GameHeader.NumItems) {
        if (Items[ct].Location == MyLoc) {
            if (f == 0) { Output(Options & YOUARE ? "\nYou can also see: " : "\nI can also see: "); f = 1; }
            else Output(" - ");
            Output(Items[ct].Text);
        }
        ct++;
    }
    Output("\n");
    Redraw = 0;     /* a fresh Look satisfies any pending redraw (we scroll,
                       unlike ScottFree's curses top window, so avoid dupes) */
}

/* ------------------------------------------------------------------ */
void LineInput(char *buf)
{
    int pos = 0, ch;
    for (;;) {
        ch = INCH();
        if (ch == 10 || ch == 13) { buf[pos] = 0; OUTCH('\n'); OutputPos = 0; return; }
        if (ch == 8 || ch == 127) { if (pos > 0) { pos--; OUTCH(8); } continue; }
        if (ch >= ' ' && ch < 127 && pos < 250) { buf[pos++] = ch; OUTCH((char)ch); }
    }
}

/* split s into up to two whitespace-delimited words (<=9 chars each) */
static int Split(char *s, char *verb, char *noun)
{
    int n = 0, i;
    W (*s == ' ' || *s == '\t') s++;
    i = 0; W (*s && *s != ' ' && *s != '\t') { if (i < 9) verb[i++] = *s; s++; }
    verb[i] = 0; if (i) n++;
    W (*s == ' ' || *s == '\t') s++;
    i = 0; W (*s && *s != ' ' && *s != '\t') { if (i < 9) noun[i++] = *s; s++; }
    noun[i] = 0; if (i) n++;
    return n;
}

static char gibuf[128];         /* GetInput line buffer (off the C stack) */

void GetInput(int *vb, int *no)
{
    char verb[10], noun[10];
    char *buf = gibuf;
    int vc, nc, num;
    for (;;) {
        do {
            Output("\nTell me what to do ? ");
            LineInput(buf);
            num = Split(buf, verb, noun);
        } W (num == 0);
        if (num == 1) *noun = 0;
        /* meta-commands handled here so they work in every game's vocabulary */
        if (strcasecmp(verb, "SAVE") == 0) { SaveGame(); continue; }
        if (strcasecmp(verb, "RESTORE") == 0 || strcasecmp(verb, "LOAD") == 0) { RestoreGame(); continue; }
        if (strcasecmp(verb, "QUIT") == 0) QUITDOS();
        if (*noun == 0 && verb[1] == 0) {
            switch (up(*verb)) {
                case 'N': strcpy(verb, "NORTH"); break;
                case 'E': strcpy(verb, "EAST");  break;
                case 'S': strcpy(verb, "SOUTH"); break;
                case 'W': strcpy(verb, "WEST");  break;
                case 'U': strcpy(verb, "UP");    break;
                case 'D': strcpy(verb, "DOWN");  break;
                case 'I': strcpy(verb, "INVENTORY"); break;
            }
        }
        nc = WhichWord(verb, Nouns);
        if (nc >= 1 && nc <= 6) vc = 1;
        else { vc = WhichWord(verb, Verbs); nc = WhichWord(noun, Nouns); }
        if (vc == -1) { Output("You use word(s) I don't know! "); continue; }
        *vb = vc; *no = nc;
        break;
    }
    strcpy(NounText, noun);
}

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* Save / restore -- a compact binary state file on the mounted disk.   */
/* Layout: "MFC", adventure#, BitFlags(4), MyLoc(1), CurrentCounter(2),  */
/* SavedRoom(2), LightTime(2), Counters[16](2 ea), RoomSaved[16](2 ea),  */
/* Items[].Location(1 ea). Ints little-endian.                          */
/* ------------------------------------------------------------------ */
#define MAXSAVES 18
static char savename[16];
static char savelist[MAXSAVES][9];      /* base names of *.SAV files on disk */

static void MakeName(char *base)        /* savename = base + ".SAV" */
{
    strcpy(savename, base);
    strcat(savename, ".SAV");
}

/* enumerate the disk, collecting the base names of all *.SAV files */
static int ScanSaves(void)
{
    char ent[12]; int n = 0; char rc;
    for (rc = dir_first(ent); rc == 0 && n < MAXSAVES; rc = dir_next(ent)) {
        if (ent[8] == 'S' && ent[9] == 'A' && ent[10] == 'V') {
            int i = 0;
            W (i < 8 && ent[i] != ' ') { savelist[n][i] = ent[i]; i++; }
            savelist[n][i] = 0;
            n++;
        }
    }
    return n;
}

static int AskNumber(void)              /* read a line, parse a decimal */
{
    char *s = gibuf; int v = 0;
    LineInput(gibuf);
    W (*s == ' ') s++;
    W (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    return v;
}

static void AskName(void)               /* prompt for a base name -> savename */
{
    char *s = gibuf; char base[10]; int i = 0;
    Output("File name: ");
    LineInput(gibuf);
    W (*s == ' ') s++;
    W (*s && *s != ' ' && *s != '.' && i < 8) base[i++] = *s++;
    if (i == 0) { strcpy(base, "SAVE"); i = 4; }
    base[i] = 0;
    MakeName(base);
}

static void wint(int v) { dputb((char)v); dputb((char)(v >> 8)); }
static int  rint(void)  { int lo = dgetb(); int hi = dgetb(); return (int)(short)((lo & 0xff) | (hi << 8)); }

static void ListSaves(int n)
{
    int i;
    Output("\nSaved games:\n");
    for (i = 0; i < n; i++) { Output("  "); OutputNumber(i + 1); Output(savelist[i]); Output("\n"); }
}

void SaveGame(void)
{
    int i, ct, n;
    n = ScanSaves();
    if (n) { Output("\nExisting saves:"); for (i = 0; i < n; i++) { Output(" "); Output(savelist[i]); } Output("\n"); }
    AskName();
    if (dopen_write(savename)) { Output("\nCan't create save file.\n"); return; }
    dputb('M'); dputb('F'); dputb('C'); dputb((char)GameAdventure);
    dputb((char)BitFlags);        dputb((char)(BitFlags >> 8));
    dputb((char)(BitFlags >> 16)); dputb((char)(BitFlags >> 24));
    dputb((char)MyLoc);
    wint(CurrentCounter); wint(SavedRoom); wint(GameHeader.LightTime);
    for (i = 0; i < 16; i++) wint(Counters[i]);
    for (i = 0; i < 16; i++) wint(RoomSaved[i]);
    for (ct = 0; ct <= GameHeader.NumItems; ct++) dputb((char)Items[ct].Location);
    dclose();
    Output("\nSaved to "); Output(savename); Output("\n");
}

void RestoreGame(void)
{
    int i, ct, n, pick; long bf;
    n = ScanSaves();
    if (n == 0) { Output("\nNo saved games on the disk.\n"); return; }
    ListSaves(n);
    Output("Restore which (number, 0 = cancel)? ");
    pick = AskNumber();
    if (pick < 1 || pick > n) { Output("Cancelled.\n"); return; }
    MakeName(savelist[pick - 1]);
    if (dopen_read(savename)) { Output("\nCan't open save file.\n"); return; }
    if (dgetb() != 'M' || dgetb() != 'F' || dgetb() != 'C' || dgetb() != GameAdventure) {
        dclose(); Output("\nNot a save for this game.\n"); return;
    }
    bf  =  (long)(dgetb() & 0xff);
    bf |=  (long)(dgetb() & 0xff) << 8;
    bf |=  (long)(dgetb() & 0xff) << 16;
    bf |=  (long)(dgetb() & 0xff) << 24;
    BitFlags = bf;
    MyLoc = dgetb();
    CurrentCounter = rint(); SavedRoom = rint(); GameHeader.LightTime = rint();
    for (i = 0; i < 16; i++) Counters[i] = rint();
    for (i = 0; i < 16; i++) RoomSaved[i] = rint();
    for (ct = 0; ct <= GameHeader.NumItems; ct++) Items[ct].Location = (unsigned char)dgetb();
    dclose();
    Output("\nRestored.\n");
    Look();
}

void GameOver(void) { Output("The game is now over.\n"); QUITDOS(); }

int PerformLine(int ct)
{
    int continuation = 0;
    int param[5], pptr = 0;
    int act[4];
    int cc = 0;
    W (cc < 5) {
        int cv, dv;
        cv = Actions[ct].Condition[cc]; dv = cv / 20; cv %= 20;
        switch (cv) {
            case 0:  param[pptr++] = dv; break;
            case 1:  if (Items[dv].Location != CARRIED) return 0; break;
            case 2:  if (Items[dv].Location != MyLoc) return 0; break;
            case 3:  if (Items[dv].Location != CARRIED && Items[dv].Location != MyLoc) return 0; break;
            case 4:  if (MyLoc != dv) return 0; break;
            case 5:  if (Items[dv].Location == MyLoc) return 0; break;
            case 6:  if (Items[dv].Location == CARRIED) return 0; break;
            case 7:  if (MyLoc == dv) return 0; break;
            case 8:  if ((BitFlags & (1L << dv)) == 0) return 0; break;
            case 9:  if (BitFlags & (1L << dv)) return 0; break;
            case 10: if (CountCarried() == 0) return 0; break;
            case 11: if (CountCarried()) return 0; break;
            case 12: if (Items[dv].Location == CARRIED || Items[dv].Location == MyLoc) return 0; break;
            case 13: if (Items[dv].Location == 0) return 0; break;
            case 14: if (Items[dv].Location) return 0; break;
            case 15: if (CurrentCounter > dv) return 0; break;
            case 16: if (CurrentCounter <= dv) return 0; break;
            case 17: if (Items[dv].Location != Items[dv].InitialLoc) return 0; break;
            case 18: if (Items[dv].Location == Items[dv].InitialLoc) return 0; break;
            case 19: if (CurrentCounter != dv) return 0; break;
        }
        cc++;
    }
    act[0] = Actions[ct].Action[0]; act[2] = Actions[ct].Action[1];
    act[1] = act[0] % 150; act[3] = act[2] % 150;
    act[0] /= 150; act[2] /= 150;
    cc = 0; pptr = 0;
    W (cc < 4) {
        int a = act[cc];
        if (a >= 1 && a < 52) { Output(Messages[a]); Output("\n"); }
        else if (a > 101) { Output(Messages[a - 50]); Output("\n"); }
        else switch (a) {
            case 0: break;
            case 52:
                if (CountCarried() == GameHeader.MaxCarry) {
                    Output(Options & YOUARE ? "You are carrying too much. " : "I've too much to carry! ");
                    break;
                }
                if (Items[param[pptr]].Location == MyLoc) Redraw = 1;
                Items[param[pptr++]].Location = CARRIED; break;
            case 53: Redraw = 1; Items[param[pptr++]].Location = MyLoc; break;
            case 54: Redraw = 1; MyLoc = param[pptr++]; break;
            case 55:
            case 59: if (Items[param[pptr]].Location == MyLoc) Redraw = 1;
                     Items[param[pptr++]].Location = 0; break;
            case 56: BitFlags |= 1L << DARKBIT; break;
            case 57: BitFlags &= ~(1L << DARKBIT); break;
            case 58: BitFlags |= 1L << param[pptr++]; break;
            case 60: BitFlags &= ~(1L << param[pptr++]); break;
            case 61:
                Output(Options & YOUARE ? "You are dead.\n" : "I am dead.\n");
                BitFlags &= ~(1L << DARKBIT);
                MyLoc = GameHeader.NumRooms; Look(); break;
            case 62: { int i = param[pptr++]; Items[i].Location = param[pptr++]; Redraw = 1; break; }
            case 63: GameOver(); break;
            case 64: Look(); break;
            case 65: {
                int c = 0, n = 0;
                W (c <= GameHeader.NumItems) {
                    if (Items[c].Location == GameHeader.TreasureRoom && *Items[c].Text == '*') n++;
                    c++;
                }
                Output(Options & YOUARE ? "You have stored " : "I've stored ");
                OutputNumber(n);
                Output("treasures.  On a scale of 0 to 100, that rates ");
                OutputNumber((n * 100) / GameHeader.Treasures);
                Output(".\n");
                if (n == GameHeader.Treasures) { Output("Well done.\n"); GameOver(); }
                break;
            }
            case 66: {
                int c = 0, f = 0;
                Output(Options & YOUARE ? "You are carrying:\n" : "I'm carrying:\n");
                W (c <= GameHeader.NumItems) {
                    if (Items[c].Location == CARRIED) { if (f) Output(" - "); f = 1; Output(Items[c].Text); }
                    c++;
                }
                if (f == 0) Output("Nothing");
                Output(".\n"); break;
            }
            case 67: BitFlags |= 1L << 0; break;
            case 68: BitFlags &= ~(1L << 0); break;
            case 69:
                GameHeader.LightTime = LightRefill;
                if (Items[LIGHT_SOURCE].Location == MyLoc) Redraw = 1;
                Items[LIGHT_SOURCE].Location = CARRIED;
                BitFlags &= ~(1L << LIGHTOUTBIT); break;
            case 70: CLS(); OutputPos = 0; break;
            case 71: SaveGame(); break;
            case 72: {
                int i1 = param[pptr++], i2 = param[pptr++];
                int t = Items[i1].Location;
                if (t == MyLoc || Items[i2].Location == MyLoc) Redraw = 1;
                Items[i1].Location = Items[i2].Location; Items[i2].Location = t; break;
            }
            case 73: continuation = 1; break;
            case 74: if (Items[param[pptr]].Location == MyLoc) Redraw = 1;
                     Items[param[pptr++]].Location = CARRIED; break;
            case 75: {
                int i1 = param[pptr++], i2 = param[pptr++];
                if (Items[i1].Location == MyLoc) Redraw = 1;
                Items[i1].Location = Items[i2].Location;
                if (Items[i2].Location == MyLoc) Redraw = 1; break;
            }
            case 76: Look(); break;
            case 77: if (CurrentCounter >= 0) CurrentCounter--; break;
            case 78: OutputNumber(CurrentCounter); break;
            case 79: CurrentCounter = param[pptr++]; break;
            case 80: { int t = MyLoc; MyLoc = SavedRoom; SavedRoom = t; Redraw = 1; break; }
            case 81: { int t = param[pptr++], c1 = CurrentCounter; CurrentCounter = Counters[t]; Counters[t] = c1; break; }
            case 82: CurrentCounter += param[pptr++]; break;
            case 83: CurrentCounter -= param[pptr++]; if (CurrentCounter < -1) CurrentCounter = -1; break;
            case 84: Output(NounText); break;
            case 85: Output(NounText); Output("\n"); break;
            case 86: Output("\n"); break;
            case 87: { int p = param[pptr++], sr = MyLoc; MyLoc = RoomSaved[p]; RoomSaved[p] = sr; Redraw = 1; break; }
            case 88: break;     /* pause */
            case 89: pptr++; break;
            default: break;
        }
        cc++;
    }
    return 1 + continuation;
}

int PerformActions(int vb, int no)
{
    static int disable_sysfunc = 0;
    int d = BitFlags & (1L << DARKBIT);
    int ct = 0, fl, doagain = 0, i;

    if (vb == 1 && no == -1) { Output("Give me a direction too."); return 0; }
    if (vb == 1 && no >= 1 && no <= 6) {
        int nl;
        if (Items[LIGHT_SOURCE].Location == MyLoc || Items[LIGHT_SOURCE].Location == CARRIED) d = 0;
        if (d) Output("Dangerous to move in the dark! ");
        nl = Rooms[MyLoc].Exits[no - 1];
        if (nl != 0) { MyLoc = nl; Look(); return 0; }
        if (d) {
            Output(Options & YOUARE ? "You fell down and broke your neck. " : "I fell down and broke my neck. ");
            QUITDOS();
        }
        Output(Options & YOUARE ? "You can't go in that direction. " : "I can't go in that direction. ");
        return 0;
    }
    fl = -1;
    W (ct <= GameHeader.NumActions) {
        int vv = Actions[ct].Vocab, nv;
        if (vb != 0 && doagain && vv != 0) break;
        if (vb != 0 && !doagain && fl == 0) break;
        nv = vv % 150; vv /= 150;
        if (vv == vb || (doagain && Actions[ct].Vocab == 0)) {
            if ((vv == 0 && RandomPercent(nv)) || doagain || (vv != 0 && (nv == no || nv == 0))) {
                int f2;
                if (fl == -1) fl = -2;
                if ((f2 = PerformLine(ct)) > 0) {
                    fl = 0;
                    if (f2 == 2) doagain = 1;
                    if (vb != 0 && doagain == 0) return 0;
                }
            }
        }
        ct++;
        if (Actions[ct].Vocab != 0) doagain = 0;
    }
    if (fl != 0 && disable_sysfunc == 0) {
        if (Items[LIGHT_SOURCE].Location == MyLoc || Items[LIGHT_SOURCE].Location == CARRIED) d = 0;
        if (vb == 10 || vb == 18) {
            if (vb == 10) {
                if (strcasecmp(NounText, "ALL") == 0) {
                    int c = 0, f = 0;
                    if (d) { Output("It is dark.\n"); return 0; }
                    W (c <= GameHeader.NumItems) {
                        if (Items[c].Location == MyLoc && Items[c].AutoGet && Items[c].AutoGet[0] != '*') {
                            no = WhichWord(Items[c].AutoGet, Nouns);
                            disable_sysfunc = 1; PerformActions(vb, no); disable_sysfunc = 0;
                            if (CountCarried() == GameHeader.MaxCarry) {
                                Output(Options & YOUARE ? "You are carrying too much. " : "I've too much to carry. ");
                                return 0;
                            }
                            Items[c].Location = CARRIED; Redraw = 1;
                            OutBuf(Items[c].Text); Output(": O.K.\n"); f = 1;
                        }
                        c++;
                    }
                    if (f == 0) Output("Nothing taken.");
                    return 0;
                }
                if (no == -1) { Output("What ? "); return 0; }
                if (CountCarried() == GameHeader.MaxCarry) {
                    Output(Options & YOUARE ? "You are carrying too much. " : "I've too much to carry. ");
                    return 0;
                }
                i = MatchUpItem(NounText, MyLoc);
                if (i == -1) {
                    Output(Options & YOUARE ? "It is beyond your power to do that. " : "It's beyond my power to do that. ");
                    return 0;
                }
                Items[i].Location = CARRIED; Output("O.K. "); Redraw = 1; return 0;
            }
            if (vb == 18) {
                if (strcasecmp(NounText, "ALL") == 0) {
                    int c = 0, f = 0;
                    W (c <= GameHeader.NumItems) {
                        if (Items[c].Location == CARRIED && Items[c].AutoGet && Items[c].AutoGet[0] != '*') {
                            no = WhichWord(Items[c].AutoGet, Nouns);
                            disable_sysfunc = 1; PerformActions(vb, no); disable_sysfunc = 0;
                            Items[c].Location = MyLoc; OutBuf(Items[c].Text); Output(": O.K.\n"); Redraw = 1; f = 1;
                        }
                        c++;
                    }
                    if (f == 0) Output("Nothing dropped.\n");
                    return 0;
                }
                if (no == -1) { Output("What ? "); return 0; }
                i = MatchUpItem(NounText, CARRIED);
                if (i == -1) {
                    Output(Options & YOUARE ? "It's beyond your power to do that.\n" : "It's beyond my power to do that.\n");
                    return 0;
                }
                Items[i].Location = MyLoc; Output("O.K. "); Redraw = 1; return 0;
            }
        }
    }
    return fl;
}

int main(void)
{
    int vb, no;
    LightRefill = GameHeader.LightTime;
    CLS();
    Output("MFC SCOTTFREE v1.14\n");
    Output("A Scott Adams game driver (ScottFree 1.14, GPL).\n\n");
    Look();
    W (1) {
        if (Redraw) { Look(); Redraw = 0; }
        PerformActions(0, 0);
        if (Redraw) { Look(); Redraw = 0; }
        GetInput(&vb, &no);
        switch (PerformActions(vb, no)) {
            case -1: Output("I don't understand your command. "); break;
            case -2: Output("I can't do that yet. "); break;
        }
        if (Items[LIGHT_SOURCE].Location != DESTROYED && GameHeader.LightTime != -1) {
            GameHeader.LightTime--;
            if (GameHeader.LightTime < 1) {
                BitFlags |= 1L << LIGHTOUTBIT;
                if (Items[LIGHT_SOURCE].Location == CARRIED || Items[LIGHT_SOURCE].Location == MyLoc)
                    Output(Options & SCOTTLIGHT ? "Light has run out! " : "Your light has run out. ");
            } else if (GameHeader.LightTime < 25) {
                if (Items[LIGHT_SOURCE].Location == CARRIED || Items[LIGHT_SOURCE].Location == MyLoc) {
                    if (Options & SCOTTLIGHT) { Output("Light runs out in "); OutputNumber(GameHeader.LightTime); Output("turns. "); }
                    else if (GameHeader.LightTime % 5 == 0) Output("Your light is growing dim. ");
                }
            }
        }
    }
    return 0;
}
