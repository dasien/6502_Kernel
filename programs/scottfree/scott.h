/*
 *  ScottFree on MFC-DOS -- shared data model.
 *
 *  Derived from ScottFree 1.14 by Swansea University Computer Society
 *  (Alan Cox), distributed under the GNU GPL. See README/Acknowledgments.
 *
 *  The game database is NOT parsed on the 6502. Instead the host tool
 *  `dat2c` pre-parses a Scott Adams TRS-80 .dat into C initializers (see
 *  build.sh); this header declares the tables both sides share.
 */
#ifndef SCOTT_H
#define SCOTT_H

#define LIGHT_SOURCE 9      /* item 9 is always the light source */
#define CARRIED      255    /* Item.Location: carried by the player */
#define DESTROYED    0      /* Item.Location: out of play (room 0)  */
#define DARKBIT      15
#define LIGHTOUTBIT  16

typedef struct {
    short Unknown;
    short NumItems;
    short NumActions;
    short NumWords;     /* verb/noun count (shorter list padded)  */
    short NumRooms;
    short MaxCarry;
    short PlayerRoom;
    short Treasures;
    short WordLength;
    short LightTime;
    short NumMessages;
    short TreasureRoom;
} Header;

typedef struct {
    unsigned short Vocab;
    unsigned short Condition[5];
    unsigned short Action[2];
} Action;

typedef struct {
    char *Text;
    short Exits[6];
} Room;

typedef struct {
    char *Text;
    unsigned char Location;     /* MUST be 8-bit (CARRIED==255)    */
    unsigned char InitialLoc;
    char *AutoGet;              /* get/drop word, or 0             */
} Item;

/* Option flags (Options) */
#define YOUARE           1  /* "You are" vs "I am" */
#define SCOTTLIGHT       2  /* authentic light messages */
#define DEBUGGING        4
#define TRS80_STYLE      8
#define PREHISTORIC_LAMP 16

/* ---- tables emitted by dat2c (defined in the generated game file) ---- */
extern Header GameHeader;
extern Item   Items[];
extern Room   Rooms[];
extern char  *Verbs[];
extern char  *Nouns[];
extern char  *Messages[];
extern Action Actions[];
extern int    GameVersion;
extern int    GameAdventure;

#endif
