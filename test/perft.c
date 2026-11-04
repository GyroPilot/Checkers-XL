/* perft.c: the rules (src/rules.c) on the host, against the published perft counts of American checkers, and a
 * self-play run of the computer player at each level (positions per move).
 *   cc -O2 -I src -o test/work/perft test/perft.c src/rules.c && test/work/perft */
#include <stdio.h>
#include <string.h>
#include "rules.h"

static long Perft(signed char *bd, short side, short d)
{
    CkMove list[CK_MAXMV];
    CkBoard b;
    long t = 0;
    short i, n = CkGen(bd, side, list);
    if (d == 1) return n;
    for (i = 0; i < n; i++) { memcpy(b, bd, sizeof b); CkDo(b, &list[i]); t += Perft(b, (short)-side, (short)(d - 1)); }
    return t;
}

int main(void)
{
    static const long want[] = { 7, 49, 302, 1469, 7361, 36768, 179740, 845931 };
    static const short depth[] = { 0, 2, 4, 6, 8 };
    static const long budget[] = { 0, 400, 1500, 4000, 10000 };
    short d, lv, bad = 0;
    CkInit();
    for (d = 1; d <= 8; d++) {
        long n = Perft(ckBd, CK_BLACK, d);
        printf("perft %d: %ld %s\n", d, n, n == want[d - 1] ? "ok" : "WRONG");
        if (n != want[d - 1]) bad = 1;
    }
    for (lv = 1; lv <= 4; lv++) {                   /* level lv (black) against level 2 (white) */
        short moves = 0, n;
        long most = 0, total = 0;
        char s[64];
        CkMove mv;
        CkStart();
        while (moves < 200) {
            n = CkThink(ckSide == CK_BLACK ? depth[lv] : depth[2], ckSide == CK_BLACK ? budget[lv] : budget[2], &mv);
            if (n == 0) break;
            if (ckSide == CK_BLACK) { total += ckNodes; if (ckNodes > most) most = ckNodes; }
            ckQuiet = (mv.nCap > 0 || ckBd[mv.hop[0]] == 1 || ckBd[mv.hop[0]] == -1) ? 0 : (short)(ckQuiet + 1);
            CkDo(ckBd, &mv);
            CkText(&mv, s);
            ckSide = (short)-ckSide;
            moves++;
            if (ckQuiet >= 80) break;
        }
        printf("level %d vs 2: %d moves, %s; level %d's most positions in a move %ld, average %ld\n", lv, moves,
               n == 0 ? (ckSide == CK_BLACK ? "white wins" : "black wins") : "drawn or long", lv, most, total / (moves / 2 + 1));
    }
    return bad;
}
