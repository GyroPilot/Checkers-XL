/* net.h: the checkers table server (CK1, PROTOCOL.md in Apple-LisaCheckers) through the ESPFuji drive's mailbox.
 * Every request is one GET:  <server><path>?table=<table>&player=<name>  (/tables adds &dev=1, as LisaCheckers). */
#ifndef NET_H
#define NET_H

#define CN_SERVER  "https://lisa-gw1.tailc2f141.ts.net/checkers"
#define CN_TBMAX   6
enum { cnLobby, cnPlay, cnOver };

typedef struct {
    Boolean got;                    /* a CK1 state came back */
    short   stat, you, turn, draw;  /* you, turn, draw: CK_BLACK, CK_WHITE or 0 */
    short   moves;
    Boolean haveBd;
    CkBoard board;
    char    black[12], white[12], last[48], result[80], prompt[80];
    char    err[120];               /* the server's ERROR words (a refused move), or "" */
} CnState;

typedef struct {
    short n;
    char  id[CN_TBMAX][12], name[CN_TBMAX][32], seats[CN_TBMAX][8], st[CN_TBMAX][8];
} CnTables;

extern CnState  cn;
extern CnTables cnTb;
extern char     cnTable[12], cnName[12];

/* One request.  noErr: the reply is in cn (a state) or cnTb (/tables); otherwise why has words for the screen
 * (the drive's own ERROR words when it gave some). */
OSErr CnGet(const char *path, char *why);
/* The last move ("22x15x8") as squares, for marking the board. */
void CnLastMove(CkMove *mv);
/* Letters and digits only, lower case, 8 at most (the server drops anything else). */
void CnCleanName(const char *in, char *out);

#endif
