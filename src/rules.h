/* rules.h: American checkers (English draughts) and the computer's player, ported from LisaCheckers 1.2's
 * CKR-RULES.TEXT (REV A).  Portable C: the same file is built into Checkers XL (Retro68, 68000) and into the host
 * test (test/perft.c), which checks it against the published perft counts.
 *
 * The standard numbering 1 to 32 of the dark squares: Black's men start on 1 to 12 and move toward 32, White's on 21
 * to 32 toward 1.  Square n is row (n-1)/4 (0 = Black's back row); in even rows the dark squares are columns 1,3,5,7,
 * in odd rows 0,2,4,6.  A man is 1 or -1, a king 2 or -2. */
#ifndef RULES_H
#define RULES_H

#define CK_BLACK   1
#define CK_WHITE   (-1)
#define CK_DEAD    3            /* a piece already jumped in this move */
#define CK_MAXMV   48           /* moves in one list (the Lisa: 40, for its 32K of globals) */
#define CK_MAXHOP  11           /* squares in one capture sequence */
#define CK_MAXPLY  9            /* search depth, captures included */
#define CK_INF     30000
#define CK_WIN     20000

typedef signed char CkBoard[33];            /* [1..32] */
typedef struct {
    unsigned char nHop, nCap;
    unsigned char hop[CK_MAXHOP + 1];       /* hop[0] = from */
    unsigned char cap[CK_MAXHOP + 1];       /* cap[1..nCap]: the squares jumped */
} CkMove;

extern CkBoard ckBd;            /* the game */
extern short   ckSide;          /* who moves next */
extern short   ckQuiet;         /* moves since the last capture or man move */
extern long    ckNodes;         /* positions looked at by the last CkThink */
extern short   ckDone;          /* the depth CkThink finished */
extern long    ckSeed;          /* for choosing among equal moves */

/* Called every 32 positions while CkThink searches: let the machine breathe, and return nonzero to stop (out of
 * time, or Command-period); the unfinished depth is then dropped, as when the budget runs out.  May be NULL. */
extern int (*ckPoll)(void);

void  CkInit(void);
void  CkStart(void);
short CkGen(signed char *bd, short side, CkMove *list);     /* every legal move; returns how many */
void  CkDo(signed char *bd, const CkMove *mv);
short CkEval(const signed char *bd, short side);
/* The move for ckSide on ckBd: iterative deepening to depth, stopped past budget positions or by ckPoll.
 * Returns the number of legal moves (0: none; best untouched). */
short CkThink(short depth, long budget, CkMove *best);
void  CkText(const CkMove *mv, char *s);                    /* "11-15", "22x15x8" */
short CkRow(short sq);
short CkCol(short sq);
short CkSq(short row, short col);                           /* 0: a light square or off the board */

#endif
