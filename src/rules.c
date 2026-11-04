/* rules.c: see rules.h.  A line-by-line port of CKR-RULES.TEXT: the same generator, evaluation, negamax with
 * alpha-beta and captures followed at the leaves, iterative deepening with a position budget, and the same random
 * choice among equal moves.  Added for the Mac: ckPoll (time limit, Command-period, breathing at 5 MHz). */
#include <stdlib.h>
#include "rules.h"

CkBoard ckBd;
short   ckSide, ckQuiet, ckDone;
long    ckNodes, ckSeed;
int   (*ckPoll)(void);

static signed char ckNb[33][4];             /* neighbour by direction; 0 = off */
static long  ckBudget;
static short ckStop;

/* work space for the search: one board and one list per depth (about 13 KB) */
static CkBoard ckWBd[CK_MAXPLY + 1];
static CkMove  ckWL[CK_MAXPLY + 1][CK_MAXMV];

short CkRow(short sq) { return (short)((sq - 1) / 4); }

short CkCol(short sq)
{
    short r = (short)((sq - 1) / 4);
    return (short)(r % 2 == 0 ? 2 * ((sq - 1) % 4) + 1 : 2 * ((sq - 1) % 4));
}

short CkSq(short row, short col)
{
    if (row < 0 || row > 7 || col < 0 || col > 7) return 0;
    return (short)((row + col) % 2 == 1 ? row * 4 + col / 2 + 1 : 0);
}

/* Direction 0 = toward row 7 and column - 1, 1 = toward row 7 and column + 1, 2 = toward row 0 and column - 1,
 * 3 = toward row 0 and column + 1.  Black's men use 0 and 1. */
void CkInit(void)
{
    short sq, r, c;
    for (sq = 1; sq <= 32; sq++) {
        r = CkRow(sq); c = CkCol(sq);
        ckNb[sq][0] = (signed char)CkSq(r + 1, c - 1);
        ckNb[sq][1] = (signed char)CkSq(r + 1, c + 1);
        ckNb[sq][2] = (signed char)CkSq(r - 1, c - 1);
        ckNb[sq][3] = (signed char)CkSq(r - 1, c + 1);
    }
    ckSeed = 12345;
    CkStart();
}

void CkStart(void)
{
    short sq;
    for (sq = 1; sq <= 32; sq++) ckBd[sq] = (signed char)(sq <= 12 ? CK_BLACK : sq >= 21 ? CK_WHITE : 0);
    ckBd[0] = 0;
    ckSide = CK_BLACK;
    ckQuiet = 0;
}

static short Crown(short sq, short side) { return side == CK_BLACK ? sq >= 29 : sq <= 4; }

/* the generator's state while Jumps recurses */
static signed char *gBd;
static CkMove *gList, gCur;
static short gN, gSide;

/* Extend gCur from square at, piece (1, 2, -1 or -2) moving.  The piece's own start square is empty during the
 * search, and jumped pieces are CK_DEAD, so none is jumped twice. */
static void Jumps(short at, short piece)
{
    short dd, m, l, lo, hi, more = 0;
    signed char saved;
    if (abs(piece) == 2) { lo = 0; hi = 3; }
    else if (piece > 0) { lo = 0; hi = 1; }
    else { lo = 2; hi = 3; }
    for (dd = lo; dd <= hi; dd++) {
        m = ckNb[at][dd];
        if (!m) continue;
        l = ckNb[m][dd];
        if (!l) continue;
        if (gBd[l] == 0 && gBd[m] != CK_DEAD && gBd[m] * piece < 0 && gCur.nHop < CK_MAXHOP) {
            more = 1;
            saved = gBd[m];
            gBd[m] = CK_DEAD;
            gCur.hop[++gCur.nHop] = (unsigned char)l;
            gCur.cap[++gCur.nCap] = (unsigned char)m;
            if (abs(piece) == 1 && Crown(l, gSide)) {        /* crowned: the move ends */
                if (gN < CK_MAXMV) gList[gN++] = gCur;
            } else
                Jumps(l, piece);
            gCur.nHop--; gCur.nCap--;
            gBd[m] = saved;
        }
    }
    if (!more && gCur.nCap > 0 && gN < CK_MAXMV) gList[gN++] = gCur;
}

/* Every legal move for side: the capture sequences if there are any, otherwise the plain moves. */
short CkGen(signed char *bd, short side, CkMove *list)
{
    short sq, d, t, d0, d1;
    signed char p;
    gBd = bd; gList = list; gN = 0; gSide = side;
    for (sq = 1; sq <= 32; sq++) {                          /* captures first */
        p = bd[sq];
        if (p * side > 0) {
            gCur.nHop = 0; gCur.nCap = 0; gCur.hop[0] = (unsigned char)sq;
            bd[sq] = 0;
            Jumps(sq, p);
            bd[sq] = p;
        }
    }
    if (gN > 0) return gN;
    if (side == CK_BLACK) { d0 = 0; d1 = 1; } else { d0 = 2; d1 = 3; }
    for (sq = 1; sq <= 32; sq++) {
        p = bd[sq];
        if (p * side <= 0) continue;
        for (d = 0; d <= 3; d++)
            if (abs(p) == 2 || d == d0 || d == d1) {
                t = ckNb[sq][d];
                if (t && bd[t] == 0 && gN < CK_MAXMV) {
                    list[gN].nHop = 1; list[gN].nCap = 0;
                    list[gN].hop[0] = (unsigned char)sq; list[gN].hop[1] = (unsigned char)t;
                    gN++;
                }
            }
    }
    return gN;
}

/* Make the move on bd: lift, take the jumped pieces, land, crown. */
void CkDo(signed char *bd, const CkMove *mv)
{
    signed char p = bd[mv->hop[0]];
    short i, t;
    bd[mv->hop[0]] = 0;
    for (i = 1; i <= mv->nCap; i++) bd[mv->cap[i]] = 0;
    t = mv->hop[mv->nHop];
    if (p == CK_BLACK && t >= 29) p = 2;
    else if (p == CK_WHITE && t <= 4) p = -2;
    bd[t] = p;
}

/* The position as side sees it: men 100, kings 160, a man's progress toward its crowning row, the four centre
 * squares, and the back row kept while the other side still has men. */
short CkEval(const signed char *bd, short side)
{
    short sq, p, r, v = 0, bm = 0, wm = 0;
    for (sq = 1; sq <= 32; sq++) {
        p = bd[sq];
        if (!p) continue;
        r = (short)((sq - 1) / 4);
        if (p == 1) { v += 100 + 3 * r; bm++; }
        else if (p == -1) { v -= 100 + 3 * (7 - r); wm++; }
        else if (p == 2) v += 160;
        else if (p == -2) v -= 160;
        if (sq == 14 || sq == 15 || sq == 18 || sq == 19) v += p > 0 ? 6 : -6;
    }
    if (wm > 0) { if (bd[1] == 1) v += 8; if (bd[3] == 1) v += 8; }       /* Black's back row: 1 and 3 */
    if (bm > 0) { if (bd[30] == -1) v -= 8; if (bd[32] == -1) v -= 8; }  /* White's back row: 30 and 32 */
    return (short)(v * side);
}

static short CkRand(short n)                /* a number from 1 to n */
{
    ckSeed = (ckSeed * 1103 + 12345) % 32749;
    if (ckSeed < 0) ckSeed = -ckSeed;
    return (short)(ckSeed % n + 1);
}

/* Negamax with alpha-beta on ckWBd[ply].  Below depth 0 only captures are followed (a capture is compulsory, so it
 * is the whole list). */
static short CkSearch(short ply, short depth, short side, short alpha, short beta)
{
    short n, i, v, best;
    if (++ckNodes > ckBudget || ((ckNodes & 31) == 0 && ckPoll && ckPoll())) { ckStop = 1; return 0; }
    n = CkGen(ckWBd[ply], side, ckWL[ply]);
    if (n == 0) return (short)(-CK_WIN + ply);             /* no move: lost; sooner is worse */
    if (ply >= CK_MAXPLY || (depth <= 0 && ckWL[ply][0].nCap == 0)) return CkEval(ckWBd[ply], side);
    best = -CK_INF;
    for (i = 0; i < n; i++) {
        short sq;
        for (sq = 1; sq <= 32; sq++) ckWBd[ply + 1][sq] = ckWBd[ply][sq];
        CkDo(ckWBd[ply + 1], &ckWL[ply][i]);
        v = (short)-CkSearch((short)(ply + 1), (short)(depth - 1), (short)-side, (short)-beta, (short)-alpha);
        if (ckStop) return 0;
        if (v > best) best = v;
        if (best > alpha) alpha = best;
        if (alpha >= beta) return best;
    }
    return best;
}

/* Iterative deepening: depth 1, 2, ... up to depth, each started with the last depth's best move first, and
 * stopped when more than budget positions have been looked at or ckPoll says so (the unfinished depth is dropped;
 * ckDone says which depth was finished).  Of moves that score the same, one is taken at random. */
short CkThink(short depth, long budget, CkMove *best)
{
    short n, i, v, top, ties, d, pick, sq;
    CkMove sw;
    ckNodes = 0; ckStop = 0; ckBudget = budget; ckDone = 0;
    for (sq = 0; sq <= 32; sq++) ckWBd[0][sq] = ckBd[sq];
    n = CkGen(ckWBd[0], ckSide, ckWL[0]);
    if (n == 0) return 0;
    *best = ckWL[0][0];
    if (n == 1) return n;                                  /* forced: no need to look */
    for (d = 1; d <= depth && !ckStop; d++) {
        top = -CK_INF; ties = 0; pick = 0;
        for (i = 0; i < n && !ckStop; i++) {
            for (sq = 1; sq <= 32; sq++) ckWBd[1][sq] = ckWBd[0][sq];
            CkDo(ckWBd[1], &ckWL[0][i]);
            v = (short)-CkSearch(1, (short)(d - 1), (short)-ckSide, -CK_INF, (short)(-top + 1));
            if (ckStop) break;
            if (v > top) { top = v; pick = i; ties = 1; }
            else if (v == top && CkRand(++ties) == 1) pick = i;
        }
        if (!ckStop) {                                     /* this depth finished: keep its move */
            *best = ckWL[0][pick];
            ckDone = d;
            if (pick > 0) { sw = ckWL[0][0]; ckWL[0][0] = ckWL[0][pick]; ckWL[0][pick] = sw; }
        }
    }
    return n;
}

static char *Num(char *s, short n)
{
    if (n >= 10) *s++ = (char)('0' + n / 10);
    *s++ = (char)('0' + n % 10);
    return s;
}

void CkText(const CkMove *mv, char *s)
{
    short i;
    s = Num(s, mv->hop[0]);
    for (i = 1; i <= mv->nHop; i++) { *s++ = mv->nCap > 0 ? 'x' : '-'; s = Num(s, mv->hop[i]); }
    *s = 0;
}
