/* Checkers XL 0.1d3: American checkers on a Macintosh XL, the Mac XL counterpart of LisaCheckers 1.2, 3 Nov 2026.
 *
 * Tim, 3 Nov 2026 (through the banana): "Give moof what it needs to make the same or similar library of apps for
 * Mac xL."  This is number 4 of the library (after FujiNet XL), a port of LisaCheckers 1.2 (Lisa Pascal):
 *   - you against the Mac at the Lisa's four levels (the same search, CKR-RULES.TEXT -> rules.c), with no drive;
 *   - you against a person on another machine (a Lisa, another Mac XL, anything that speaks CK1) through the table
 *     server https://lisa-gw1.tailc2f141.ts.net/checkers, by the ESPFuji drive's mailbox (mailbox.c, as FujiNet XL).
 * Click a piece, then where it goes; dots show where it can go.  The board, the panel, the keys and the messages
 * follow the Lisa's.  Sized for a stock XL: 1 MB, 5 MHz, a 512 x 342 screen at least (SIZE 192K).  At 5 MHz the
 * search is bounded by the Lisa's position budget per level and by a time limit per level, and lets the machine
 * breathe (and Command-period stop it: "move now") while it thinks.
 *
 * 68000 code, System 6 and 7 (MacWorks Plus and Plus II), built with Retro68.
 */
#include <Multiverse.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "rules.h"
#include "mailbox.h"
#include "net.h"

#define kVersion   "Checkers XL 1.0"
#define kSettings  "\pCheckers XL Settings"

enum { mApple = 128, mFile, mEdit, mGame, mLevel, mNet };
enum { iAbout = 1 };
enum { iQuit = 1 };
enum { iNew = 1, iUndo, iDots = 4, iNums, iSides, iHow = 8, iKeys };
enum { iPlay = 1, iLeave, iDraw = 4, iResign, iAgain, iLook = 8, iName };
enum { kNote = 130, kCaution = 131, kHowAlert = 132, kKeysAlert = 133, kAboutDlog = 129, kAskDlog = 128 };

#define kCell     32
#define kMaxHist  30
#define kMaxPath  12
#define kDrawPly  80                             /* 40 moves each with only kings moving: a draw */
#define kGap      3                              /* seconds between looks at the server, as the Lisa */
#define kGapMine  15                             /* while it is your move (to notice a resignation), as the Lisa */

enum { vwBoard, vwTables };

/* the levels: the Lisa's depth and position budget, and (new for 5 MHz) a time limit in seconds */
static const char  *kLvName[5] = { "", "Easy", "Normal", "Hard", "Hardest" };
static const short  kLvDepth[5] = { 0, 2, 4, 6, 8 };
static const long   kLvBudget[5] = { 0, 400, 1500, 4000, 10000 };
static const short  kLvSecs[5] = { 0, 3, 8, 15, 30 };

static WindowPtr gWin;
static Boolean   gQuit, gHasWNE;
static short     gWinW, gWinH, gBX, gBY, gPX;    /* the window, the board's corner, the panel's left edge */

/* the game (the board itself is the rules' ckBd and ckSide) */
static short   gHuman = CK_BLACK, gLevel = 2;
static Boolean gDots = true, gNums, gOver, gStarted, gThinking, gMoveNow;
static CkMove  gLegal[CK_MAXMV];                 /* your legal moves, this turn */
static short   gNLegal;
static short   gPath[kMaxPath + 1], gNPath = -1; /* squares clicked so far; -1 = nothing picked */
static short   gCurR = 5, gCurC = 2;             /* the keyboard cursor, screen rows and columns */
static CkMove  gLastYou, gLastMac;
static Boolean gHaveYou, gHaveMac;
static long    gNodes, gThinkTicks;           /* the last search: positions, and how long it took */
static char    gStatus[256];
static struct { CkBoard bd; short quiet; } gHist[kMaxHist];
static short   gNHist;
static short   gSig[8][8];                       /* what each square looked like when last drawn */
static Boolean gBoardOk;

/* the network game */
static short   gView = vwBoard;
static Boolean gNet, gPollPause, gNetErr;
static unsigned long gLastPoll;
static short   gLastMvCnt = -1;

/* while the Mac thinks */
static unsigned long gThinkEnd, gLastBreath, gPollTick, gLastBreath0;

/* ---------- small things ---------- */

static Boolean TrapAvailable(short trap)
{
    return NGetTrapAddress(trap, kToolboxTrapType) != NGetTrapAddress(0xA89F, kToolboxTrapType);
}

static void CtoP(const char *c, unsigned char *p)
{
    size_t n = strlen(c);
    if (n > 255) n = 255;
    p[0] = (unsigned char)n;
    memcpy(p + 1, c, n);
}

static void PtoC(const unsigned char *p, char *c) { memcpy(c, p + 1, p[0]); c[p[0]] = 0; }

static void Text(short x, short y, const char *s) { MoveTo(x, y); DrawText((Ptr)s, 0, (short)strlen(s)); }

/* While a request waits: stop on Command-period (or Escape).  FujiNet XL 0.1d2's: on System 7 no key event reaches a
 * program while it is busy like this, so the keys themselves are read with GetKeys, and a press is remembered for
 * half a second (a quick tap shows Command first, then period). */
Boolean MbCancelPressed(void)
{
    static unsigned long cmdAt, dotAt;
    EventRecord e;
    unsigned char k[16];                         /* the KeyMap: key code n is bit n & 7 of byte n >> 3 */
    unsigned long now = TickCount();
    if (EventAvail(keyDownMask, &e)) {           /* where key events do come, take them too */
        char c = (char)(e.message & charCodeMask);
        if (((e.modifiers & cmdKey) && (c == '.' || c == '>')) || c == 0x1B) { GetNextEvent(keyDownMask, &e); return true; }
    }
    GetKeys((void *)k);
    if ((k[0x37 >> 3] >> (0x37 & 7)) & 1) cmdAt = now;           /* Command */
    if ((k[0x2F >> 3] >> (0x2F & 7)) & 1) dotAt = now;           /* period */
    if ((k[0x35 >> 3] >> (0x35 & 7)) & 1) return true;           /* Escape */
    if (cmdAt && dotAt && now - cmdAt < 30 && now - dotAt < 30) { cmdAt = dotAt = 0; return true; }
    return false;
}

/* Sleep while a request waits, the keyboard still heard.  It takes no key events out of the queue (MbCancelPressed
 * takes only Command-period and Escape): a key or click made during a look at the server is kept for afterwards.
 * (Salvo XL, 3 Nov 2026: FujiNet XL's MbIdle took keys off the queue, and an L typed during a poll was lost.) */
Boolean MbIdle(short ticks)
{
    EventRecord e;
    if (gHasWNE) WaitNextEvent(0, &e, ticks, NULL);
    else { unsigned long t = TickCount() + ticks; while (TickCount() < t) SystemTask(); }
    return MbCancelPressed();
}

void MbBreathe(void)                             /* let the drive work and other programs run */
{
    EventRecord e;
    if (gHasWNE) WaitNextEvent(0, &e, 1, NULL);
    else SystemTask();
}

/* The search's hook, every 32 positions: at most once a tick look for Command-period (move now), every 10 ticks
 * let other programs run, and stop at the level's time limit (at 5 MHz the Lisa's budgets can take a while). */
static int ThinkPoll(void)
{
    unsigned long now = TickCount();
    if (now == gPollTick) return 0;
    gPollTick = now;
    if (MbCancelPressed()) { gMoveNow = true; return 1; }
    if (now - gLastBreath >= 10) {
        EventRecord e;
        gLastBreath = now;
        if (gHasWNE) WaitNextEvent(0, &e, 0, NULL); else SystemTask();
    }
    return now >= gThinkEnd;
}

static short Think(short level, CkMove *mv)
{
    short n;
    gMoveNow = false;
    gLastBreath = gPollTick = gLastBreath0 = TickCount();
    gThinkEnd = gLastBreath + (unsigned long)kLvSecs[level] * 60;
    SetCursor(*GetCursor(watchCursor));
    n = CkThink(kLvDepth[level], kLvBudget[level], mv);
    InitCursor();
    gNodes = ckNodes;
    gThinkTicks = (long)(TickCount() - gLastBreath0);
    return n;
}

static void Alert1(short id, const char *s)
{
    Str255 p;
    CtoP(s, p);
    ParamText(p, "\p", "\p", "\p");
    InitCursor();
    if (id == kCaution) CautionAlert(id, NULL); else NoteAlert(id, NULL);
    SetPort(gWin);
}

static Boolean Ask(const char *s)                /* OK or Cancel */
{
    Str255 p;
    short item;
    CtoP(s, p);
    ParamText(p, "\p", "\p", "\p");
    InitCursor();
    item = CautionAlert(kCaution, NULL);
    SetPort(gWin);
    return item == 1;
}

/* ---------- the name, kept beside the application ---------- */

static void SaveName(void)
{
    short ref, vref;
    long n;
    char s[40];
    GetVol(NULL, &vref);
    if (FSOpen(kSettings, vref, &ref) == fnfErr) { Create(kSettings, vref, 'CKXL', 'TEXT'); if (FSOpen(kSettings, vref, &ref) != noErr) return; }
    sprintf(s, "name=%s\r", cnName);
    n = (long)strlen(s);
    SetEOF(ref, 0); FSWrite(ref, &n, s); FSClose(ref);
}

static void LoadName(void)
{
    short ref, vref;
    long n = 100;
    char s[101], name[40], *p;
    GetVol(NULL, &vref);
    if (FSOpen(kSettings, vref, &ref) != noErr) return;
    FSRead(ref, &n, s); FSClose(ref); s[n] = 0;
    for (p = s; *p; p++) if (*p == '\r') *p = '\n';
    if ((p = strstr(s, "name=")) != NULL && sscanf(p + 5, "%39[^\n]", name) == 1) {
        CnCleanName(name, name);
        if (name[0]) strcpy(cnName, name);
    }
}

static Boolean AskText(const char *prompt, char *text, short max)
{
    DialogPtr d = GetNewDialog(kAskDlog, NULL, (WindowPtr)-1);
    short item = 0, type;
    Handle h;
    Rect r;
    Str255 p;
    if (!d) return false;
    GetDialogItem(d, 3, &type, &h, &r); CtoP(prompt, p); SetDialogItemText(h, p);
    GetDialogItem(d, 4, &type, &h, &r); CtoP(text, p); SetDialogItemText(h, p);
    SelectDialogItemText(d, 4, 0, 255);
    ShowWindow(d);
    InitCursor();
    while (item != 1 && item != 2) ModalDialog(NULL, &item);
    GetDialogItemText(h, p);
    DisposeDialog(d);
    SetPort(gWin);
    if (item != 1) return false;
    if (p[0] > max) p[0] = (unsigned char)max;
    PtoC(p, text);
    return true;
}

/* ---------- the board on the screen ---------- */

/* The square at screen row sr, column sc (row 0 at the top).  Your side is always at the bottom: as black the board
 * is turned round. */
static short ScrSq(short sr, short sc)
{
    return gHuman == CK_BLACK ? CkSq((short)(7 - sr), (short)(7 - sc)) : CkSq(sr, sc);
}

static void CellRect(short sr, short sc, Rect *rc)
{
    short x = (short)(gBX + sc * kCell), y = (short)(gBY + sr * kCell);
    SetRect(rc, x, y, (short)(x + kCell), (short)(y + kCell));
}

static Boolean Picked(short sq)                  /* the picked piece or a square clicked after it */
{
    short i;
    for (i = 0; i <= gNPath; i++) if (gPath[i] == sq) return true;
    return false;
}

static Boolean Matches(const CkMove *mv)         /* starts at gPath[0] and passes the squares clicked after it */
{
    short i, j = 0;
    if (gNPath < 0 || mv->hop[0] != gPath[0]) return false;
    for (i = 1; i <= gNPath; i++) {
        Boolean found = false;
        while (!found && j < mv->nHop) if (mv->hop[++j] == gPath[i]) found = true;
        if (!found) return false;
    }
    return true;
}

static Boolean CanGo(short sq)                   /* a move that fits the clicks so far can land on sq */
{
    short i, j;
    if (gNPath < 0) return false;
    for (i = 0; i < gNLegal; i++)
        if (Matches(&gLegal[i]))
            for (j = 1; j <= gLegal[i].nHop; j++) if (gLegal[i].hop[j] == sq) return true;
    return false;
}

static Boolean MarkMac(short sq)                 /* a square of the other side's last move */
{
    short i;
    if (gHaveMac) for (i = 0; i <= gLastMac.nHop; i++) if (gLastMac.hop[i] == sq) return true;
    return false;
}

/* One square: light, or gray with its piece; the picked piece marked in its corners, dots where it can go, the other
 * side's last move marked top left, the cursor as a double frame, the number when # is on. */
static void DrawSq(short sr, short sc)
{
    Rect rc, d;
    short sq = ScrSq(sr, sc), p;
    CellRect(sr, sc, &rc);
    SetRect(&d, (short)(rc.left + 1), (short)(rc.top + 1), rc.right, rc.bottom);
    if (!sq) { EraseRect(&d); return; }
    PenNormal();
    FillRect(&d, &qd.gray);
    p = ckBd[sq];
    SetRect(&d, (short)(rc.left + 4), (short)(rc.top + 4), (short)(rc.right - 3), (short)(rc.bottom - 3));
    if (p > 0) {                                 /* black: solid */
        EraseOval(&d); InsetRect(&d, 1, 1); PaintOval(&d);
        if (p == 2) { InsetRect(&d, 6, 6); EraseOval(&d); InsetRect(&d, 3, 3); PaintOval(&d); }   /* a king: a ring */
    } else if (p < 0) {                          /* white: outlined */
        EraseOval(&d); FrameOval(&d);
        if (p == -2) { InsetRect(&d, 7, 7); PaintOval(&d); InsetRect(&d, 3, 3); EraseOval(&d); }
    } else if (gDots && !gThinking && !gOver && CanGo(sq)) {
        SetRect(&d, (short)(rc.left + 10), (short)(rc.top + 10), (short)(rc.left + 23), (short)(rc.top + 23));
        EraseOval(&d); InsetRect(&d, 3, 3); PaintOval(&d);
    }
    if (gNPath >= 0 && Picked(sq)) {
        SetRect(&d, (short)(rc.left + 1), (short)(rc.top + 1), (short)(rc.left + 6), (short)(rc.top + 6)); PaintRect(&d);
        SetRect(&d, (short)(rc.right - 5), (short)(rc.top + 1), rc.right, (short)(rc.top + 6)); PaintRect(&d);
        SetRect(&d, (short)(rc.left + 1), (short)(rc.bottom - 5), (short)(rc.left + 6), rc.bottom); PaintRect(&d);
        SetRect(&d, (short)(rc.right - 5), (short)(rc.bottom - 5), rc.right, rc.bottom); PaintRect(&d);
    }
    if (MarkMac(sq)) {
        SetRect(&d, (short)(rc.left + 2), (short)(rc.top + 2), (short)(rc.left + 9), (short)(rc.top + 9)); EraseRect(&d);
        InsetRect(&d, 1, 1); PaintRect(&d);
    }
    if (gNums) {
        char s[8];
        sprintf(s, "%d", sq);
        SetRect(&d, (short)(rc.right - 13), (short)(rc.bottom - 11), rc.right, rc.bottom); EraseRect(&d);
        Text((short)(rc.right - 12), (short)(rc.bottom - 2), s);
    }
    if (sr == gCurR && sc == gCurC) {
        SetRect(&d, (short)(rc.left + 1), (short)(rc.top + 1), rc.right, rc.bottom); FrameRect(&d);
        InsetRect(&d, 1, 1); FrameRect(&d);
    }
}

static short SqSig(short sr, short sc)           /* everything DrawSq shows on one square, as one number */
{
    short sq = ScrSq(sr, sc), g;
    if (!sq) return -1;
    g = (short)(ckBd[sq] + 3);
    if (ckBd[sq] == 0 && gDots && !gThinking && !gOver && CanGo(sq)) g += 8;
    if (gNPath >= 0 && Picked(sq)) g += 16;
    if (MarkMac(sq)) g += 32;
    if (sr == gCurR && sc == gCurC) g += 64;
    if (gNums) g += 128;
    return g;
}

static void DrawDiff(void)                       /* repaint only the squares whose look changed */
{
    short sr, sc, g;
    for (sr = 0; sr < 8; sr++)
        for (sc = 0; sc < 8; sc++)
            if ((g = SqSig(sr, sc)) != gSig[sr][sc]) { DrawSq(sr, sc); gSig[sr][sc] = g; }
}

static void DrawBoard(void)
{
    short i;
    Rect r;
    SetRect(&r, 0, 0, (short)(gPX - 4), gWinH);
    EraseRect(&r);
    PenNormal();
    for (i = 0; i <= 8; i++) {
        MoveTo((short)(gBX + i * kCell), gBY); LineTo((short)(gBX + i * kCell), (short)(gBY + 8 * kCell));
        MoveTo(gBX, (short)(gBY + i * kCell)); LineTo((short)(gBX + 8 * kCell), (short)(gBY + i * kCell));
    }
    for (i = 0; i < 64; i++) { DrawSq((short)(i / 8), (short)(i % 8)); gSig[i / 8][i % 8] = SqSig((short)(i / 8), (short)(i % 8)); }
    gBoardOk = true;
}

/* The status panel on the right of the board.  Geneva 9; the message line wraps in a box. */
static void DrawPanel(void)
{
    Rect r;
    short x = (short)(gPX + 4), y = (short)(gBY + 10), sq, nb = 0, nw = 0, kb = 0, kw = 0;
    char s[120], a[24], b[24];
    const char *opp;
    SetPort(gWin);
    SetRect(&r, (short)(gPX - 4), 0, gWinW, gWinH);
    EraseRect(&r);
    for (sq = 1; sq <= 32; sq++) {
        if (ckBd[sq] > 0) { nb++; if (ckBd[sq] == 2) kb++; }
        else if (ckBd[sq] < 0) { nw++; if (ckBd[sq] == -2) kw++; }
    }
    TextFace(bold); Text(x, y, kVersion); TextFace(0);
    if (gHuman == CK_BLACK) sprintf(s, "You (black): %d pieces, %d kings", nb, kb);
    else sprintf(s, "You (white): %d pieces, %d kings", nw, kw);
    Text(x, (short)(y + 20), s);
    opp = "Mac";
    if (gNet) { opp = gHuman == CK_BLACK ? cn.white : cn.black; if (!opp[0] || !strcmp(opp, "-")) opp = "(nobody yet)"; }
    if (gHuman == CK_BLACK) sprintf(s, "%s: %d pieces, %d kings", opp, nw, kw);
    else sprintf(s, "%s: %d pieces, %d kings", opp, nb, kb);
    Text(x, (short)(y + 34), s);
    if (gNet) sprintf(s, "Network table %s, you: %s", cnTable, cnName);
    else sprintf(s, "Level %d -- %s", gLevel, kLvName[gLevel]);
    Text(x, (short)(y + 54), s);
    SetRect(&r, x, (short)(y + 64), (short)(gWinW - 6), (short)(y + 64 + 36));
    TETextBox((Ptr)gStatus, (long)strlen(gStatus), &r, teFlushDefault);
    if (gNet) {
        sprintf(s, "Last move: %s", cn.last[0] ? cn.last : "-");
        Text(x, (short)(y + 114), s);
        if (cn.draw && cn.stat == cnPlay) Text(x, (short)(y + 128), cn.draw == cn.you ? "You offered a draw." : "A draw is offered (Network menu).");
        else { sprintf(s, "%d moves so far.", cn.moves); Text(x, (short)(y + 128), s); }
    } else {
        if (gHaveYou) CkText(&gLastYou, a); else strcpy(a, "--");
        if (gHaveMac) CkText(&gLastMac, b); else strcpy(b, "--");
        sprintf(s, "Last move: you %s, Mac %s", a, b);
        Text(x, (short)(y + 114), s);
        sprintf(s, "Looked at %ld positions in %ld.%ld s.", gNodes, gThinkTicks / 60, gThinkTicks % 60 / 6);
        Text(x, (short)(y + 128), s);
    }
    y = (short)(y + 160);
    Text(x, y, "Click a piece, then where it goes.");
    Text(x, (short)(y + 14), "Arrows or J L I K, and Return, too.");
    if (gNet) {
        Text(x, (short)(y + 28), "Network menu: draw, resign, leave.");
        Text(x, (short)(y + 42), "A = another game when it is over.");
        Text(x, (short)(y + 56), "R looks at the table again.");
    } else {
        Text(x, (short)(y + 28), "N new  U undo  D dots  # numbers");
        Text(x, (short)(y + 42), "Apple-1 to 4 level  S switch  H help");
    }
}

static void DrawTables(void)
{
    Rect r = gWin->portRect;
    short i, y = 20;
    char s[120];
    EraseRect(&r);
    TextFace(bold); Text(12, y, kVersion " -- play checkers with people on other machines"); TextFace(0);
    sprintf(s, "Your name: %s      (N changes it)", cnName);
    Text(12, (short)(y + 22), s);
    Text(12, (short)(y + 44), "Tables on the checkers server (lisa-gw1):");
    for (i = 0; i < cnTb.n; i++) {
        sprintf(s, "%d", i + 1); Text(24, (short)(y + 64 + i * 16), s);
        Text(40, (short)(y + 64 + i * 16), cnTb.name[i]);
        sprintf(s, "%s players", cnTb.seats[i]); Text(150, (short)(y + 64 + i * 16), s);
        Text(230, (short)(y + 64 + i * 16), cnTb.st[i]);
    }
    y = (short)(y + 64 + CN_TBMAX * 16 + 8);
    Text(12, y, "Type a table's number (or click it) to sit down.");
    Text(12, (short)(y + 14), "R look again   N name   Q back to the board");
    SetRect(&r, 12, (short)(y + 26), (short)(gWinW - 12), (short)(y + 26 + 48));
    TETextBox((Ptr)gStatus, (long)strlen(gStatus), &r, teFlushDefault);
}

static void DrawShow(void)
{
    SetPort(gWin);
    if (gView == vwTables) { DrawTables(); gBoardOk = false; return; }
    if (gBoardOk) DrawDiff(); else DrawBoard();
    DrawPanel();
}

static void Say(const char *s)                /* a new message line, and the panel */
{
    strncpy(gStatus, s, sizeof gStatus - 1);
    gStatus[sizeof gStatus - 1] = 0;
    SetPort(gWin);
    if (gView == vwTables) DrawTables(); else DrawPanel();
}

/* ---------- the game ---------- */

static void StartTurn(void)                      /* your turn: your legal moves, nothing picked */
{
    gNLegal = CkGen(ckBd, gHuman, gLegal);
    gNPath = -1;
    DrawShow();
}

static void FinishGame(short who)                /* who: the winner, 0 for a draw */
{
    gOver = true; gThinking = false; gNLegal = 0; gNPath = -1;
    strcpy(gStatus, who == gHuman ? "Game over: you win." : who ? "Game over: the Mac wins." : "Game over: a draw.");
    DrawShow();
    if (who == gHuman) Alert1(kNote, "Game over -- you win!\r\rN starts a new game.");
    else if (who) Alert1(kNote, "Game over -- the Mac wins.\r\rN starts a new game; U takes back your last move if you want to try a different ending.");
    else Alert1(kNote, "Game over -- a draw.\r\rForty moves each went by with only kings moving and nothing taken.  N starts a new game.");
}

static void Played(const CkMove *mv)             /* the count toward a draw, then whose turn it is */
{
    if (mv->nCap > 0 || abs(ckBd[mv->hop[mv->nHop]]) == 1) ckQuiet = 0; else ckQuiet++;
    ckSide = (short)-ckSide;
}

static void MacTurn(void)                        /* the Mac moves; then it is your turn, or the game is over */
{
    CkMove mv;
    short n;
    gThinking = true;
    Say("The Mac is thinking...  (Apple-period: move now)");
    n = Think(gLevel, &mv);
    gThinking = false;
    if (n == 0) { FinishGame(gHuman); return; }  /* the Mac cannot move */
    CkDo(ckBd, &mv);
    Played(&mv);
    gLastMac = mv; gHaveMac = true;
    gNLegal = CkGen(ckBd, gHuman, gLegal);
    if (gNLegal == 0) { FinishGame((short)-gHuman); return; }
    if (ckQuiet >= kDrawPly) { FinishGame(0); return; }
    strcpy(gStatus, gLegal[0].nCap > 0 ? "Your move.  You must jump." : "Your move.");
    StartTurn();
}

/* ---------- the network game: a table on the checkers server, which keeps the board and checks every move ---------- */

static Boolean NetReady(void)                    /* the mailbox, opened at the first network request */
{
    char where[120], s[200];
    OSErr err;
    if (MbReady()) return true;
    err = MbOpen(where);
    if (err) {
        sprintf(s, "No mailbox (error %d): the network game needs the ESPFuji drive (firmware 1.1 or later) and 32 KB on it.", (int)err);
        Say(s);
        return false;
    }
    return true;
}

static void NetSync(void)                        /* the server's state onto the board */
{
    if (cn.haveBd) memcpy(ckBd, cn.board, sizeof ckBd);
    gHuman = cn.you ? cn.you : CK_BLACK;         /* watching: black at the bottom */
    if (cn.turn) ckSide = cn.turn;
    CnLastMove(&gLastMac);
    gHaveMac = gLastMac.nHop > 0;
    gHaveYou = false;
    gOver = cn.stat == cnOver;
    gThinking = false;
    if (cn.moves != gLastMvCnt) { gNPath = -1; gLastMvCnt = cn.moves; }   /* a new position: nothing picked */
    if (cn.stat == cnPlay && cn.you && cn.turn == cn.you) {
        gNLegal = CkGen(ckBd, gHuman, gLegal);
        strcpy(gStatus, gNLegal > 0 && gLegal[0].nCap > 0 ? "Your move.  You must jump." : "Your move.");
    } else {
        gNLegal = 0; gNPath = -1;
        if (cn.stat == cnOver) {
            sprintf(gStatus, "%s.", cn.result);
            if (gStatus[0] >= 'a' && gStatus[0] <= 'z') gStatus[0] = (char)(gStatus[0] - 32);
        }
        else strcpy(gStatus, cn.prompt);
    }
}

/* The server's game has just gone over: say who won (the name before " wins" in the result; none = a draw). */
static void NetEnd(short wasSt)
{
    const char *me, *k, *st;
    char who[40];
    short n;
    if (cn.stat != cnOver || wasSt == cnOver || !cn.you) return;
    me = gHuman == CK_BLACK ? cn.black : cn.white;
    if ((k = strstr(cn.result, " wins")) == NULL) { Alert1(kNote, "Game over -- a draw.\r\rA asks for another game (the colours swap)."); return; }
    for (st = k; st > cn.result && st[-1] != ' '; st--) ;      /* back to the start of the name */
    n = (short)(k - st);
    if (n > 39) n = 39;
    memcpy(who, st, n); who[n] = 0;
    if (!strcmp(who, me)) Alert1(kNote, "Game over -- you win!\r\rA asks for another game (the colours swap).");
    else Alert1(kNote, "Game over -- your opponent wins this one.\r\rA asks for another game (the colours swap).");
}

/* One request; the window is redrawn with what came back.  A request stopped (Command-period) or unanswered (no
 * drive) pauses the polling until you ask again (R); a drive or server error slows it to every 15 s. */
static Boolean NetAsk(const char *path)
{
    char why[256];
    short oSt = cn.stat;
    OSErr err;
    Boolean state = strcmp(path, "/tables") && strcmp(path, "/leave");
    if (!NetReady()) return false;
    Say("Talking to the checkers server ...  (Apple-period stops)");
    SetCursor(*GetCursor(watchCursor));
    err = CnGet(path, why);
    InitCursor();
    gLastPoll = TickCount();
    gPollPause = err == 1 || err == userCanceledErr;
    gNetErr = err != noErr;
    if (err == noErr && state) NetSync();
    else if (err == noErr) gStatus[0] = 0;
    else snprintf(gStatus, sizeof gStatus, "%s%s", why, gPollPause && gNet ? "  R looks again." : "");
    if (err == noErr && cn.err[0]) sprintf(gStatus, "Server: %s", cn.err);
    DrawShow();
    if (err == noErr && state) NetEnd(oSt);
    return err == noErr;
}

/* The idle look at the server: no "Talking" line, and only what changed is repainted. */
static void NetPoll(void)
{
    char why[256], oStatus[256];
    short oSt = cn.stat, oMv = cn.moves, oTn = cn.turn, oDr = cn.draw;
    CkBoard oBd;
    char oBl[12], oWh[12];
    OSErr err;
    memcpy(oBd, ckBd, sizeof oBd);
    strcpy(oBl, cn.black); strcpy(oWh, cn.white); strcpy(oStatus, gStatus);
    err = CnGet("/state", why);
    gLastPoll = TickCount();
    gPollPause = err == 1 || err == userCanceledErr;
    gNetErr = err != noErr;
    if (err == noErr) NetSync();
    else snprintf(gStatus, sizeof gStatus, "%s%s", why, gPollPause ? "  R looks again." : "");
    if (err == noErr && cn.err[0]) sprintf(gStatus, "Server: %s", cn.err);
    if (memcmp(oBd, ckBd, sizeof oBd) || cn.moves != oMv || cn.stat != oSt || cn.turn != oTn || cn.draw != oDr
        || strcmp(oBl, cn.black) || strcmp(oWh, cn.white)) DrawShow();
    else {
        if (err == noErr && gNPath >= 0) strcpy(gStatus, oStatus);   /* a piece is picked: keep its message */
        if (strcmp(gStatus, oStatus)) DrawPanel();
    }
    NetEnd(oSt);
}

static void IdlePoll(void)                       /* every 3 s at a table; every 15 s while it is your move */
{
    unsigned long gap;
    if (!gNet || gView != vwBoard || gThinking || gPollPause) return;
    gap = (unsigned long)(gNLegal > 0 || gNetErr ? kGapMine : kGap) * 60;
    if (TickCount() - gLastPoll >= gap) NetPoll();
}

static void NetSend(const CkMove *mv)            /* your move, to the server; the board comes back from it */
{
    char s[64], path[80];
    CkText(mv, s);
    sprintf(path, "/move/%s", s);
    gThinking = true;
    if (!NetAsk(path)) {
        gThinking = false;
        gLastPoll = TickCount() - kGapMine * 60; /* look again at once: the server may have the move */
    }
}

/* ---------- moves ---------- */

static void YouMove(const CkMove *mv)            /* your move is complete: make it, then the Mac's reply */
{
    if (gNet) { NetSend(mv); return; }
    if (gNHist < kMaxHist) { memcpy(gHist[gNHist].bd, ckBd, sizeof ckBd); gHist[gNHist].quiet = ckQuiet; gNHist++; }
    CkDo(ckBd, mv);
    Played(mv);
    gLastYou = *mv; gHaveYou = true;
    gStarted = true;
    gNPath = -1; gNLegal = 0;
    if (ckQuiet >= kDrawPly) { FinishGame(0); return; }
    gThinking = true;
    DrawShow();
    MacTurn();
}

static void PickSq(short sq)                     /* a click (or Return) on square sq: pick a piece, or say where it goes */
{
    short i, n = 0, last = 0, one = 0;
    if (gOver || gThinking || !sq) return;
    if (ckBd[sq] * gHuman > 0) {                 /* pick this piece */
        for (i = 0; i < gNLegal; i++) if (gLegal[i].hop[0] == sq) n++;
        if (n == 0) {
            strcpy(gStatus, gNLegal == 0 ? (gNet ? "It is not your move." : "That piece cannot move.")
                          : gLegal[0].nCap > 0 ? "That piece cannot move: you must jump." : "That piece cannot move.");
            gNPath = -1;
        } else { gNPath = 0; gPath[0] = sq; strcpy(gStatus, "Now click where it goes."); }
        DrawShow();
        return;
    }
    if (gNPath < 0) { Say("First click one of your pieces."); return; }
    if (gNPath < kMaxPath) gPath[++gNPath] = sq;
    for (i = 0; i < gNLegal; i++)                /* moves that fit the clicks, and end where the last click was */
        if (Matches(&gLegal[i])) {
            n++;
            if (gLegal[i].hop[gLegal[i].nHop] == sq) { last++; one = i; }
        }
    if (n == 0) { gNPath--; strcpy(gStatus, "It cannot go there."); DrawShow(); return; }
    if (n == 1 && last == 1) { CkMove mv = gLegal[one]; YouMove(&mv); return; }
    strcpy(gStatus, last >= 1 ? "Two ways to get there: click each landing square." : "Keep going: click the next landing square.");
    DrawShow();
}

static void NewGame(void)
{
    CkStart();
    gNHist = 0; gOver = gStarted = gThinking = gHaveYou = gHaveMac = false;
    gNodes = 0; gThinkTicks = 0; gNPath = -1; gCurR = 5; gCurC = 2;
    if (gHuman == CK_BLACK) { strcpy(gStatus, "Your move.  Black moves first."); StartTurn(); }
    else { strcpy(gStatus, "The Mac is black and opens."); gNLegal = 0; DrawShow(); MacTurn(); }
}

static void UndoMove(void)                       /* back to before your last move (the Mac's reply goes too) */
{
    if (gThinking) return;
    if (gNet) { Say("No take-backs in a network game."); return; }
    if (gNHist == 0) { Say("Nothing to take back."); return; }
    gNHist--;
    memcpy(ckBd, gHist[gNHist].bd, sizeof ckBd);
    ckQuiet = gHist[gNHist].quiet;
    ckSide = gHuman;
    gOver = gHaveYou = gHaveMac = false;
    gNodes = 0; gThinkTicks = 0;
    gStarted = gNHist > 0;
    strcpy(gStatus, "Took back your last move and the reply.");
    StartTurn();
}

static void AskNewGame(void)
{
    if (gNet) { Say("In a network game: Another Game, or Leave (Network menu)."); return; }
    if (gStarted && !gOver && !Ask("Start a new game?\r\rThe game in progress is abandoned.")) return;
    NewGame();
}

static void SwitchSides(void)
{
    if (gNet) { Say("The server gives the colours (they swap each game)."); return; }
    if (gStarted && !gOver && !Ask("Start a new game?\r\rThe game in progress is abandoned.")) return;
    gHuman = (short)-gHuman;
    gBoardOk = false;
    NewGame();
}

static void SetLevel(short n)
{
    short i;
    if (n < 1 || n > 4) return;
    gLevel = n;
    for (i = 1; i <= 4; i++) CheckItem(GetMenuHandle(mLevel), i, i == n);
    DrawPanel();
}

static void MoveCursor(short dr, short dc)
{
    gCurR = (short)(gCurR + dr); gCurC = (short)(gCurC + dc);
    if (gCurR < 0) gCurR = 0;
    if (gCurR > 7) gCurR = 7;
    if (gCurC < 0) gCurC = 0;
    if (gCurC > 7) gCurC = 7;
    SetPort(gWin);
    DrawDiff();
}

/* The test key (!): the Mac picks your move at level 4 and plays it, as if you had clicked it (for the QEMU tests,
 * so that a game against a weaker level ends; harmless in a game). */
static void PlayForMe(void)
{
    CkMove mv;
    if (gOver || gThinking || gNLegal == 0 || gView != vwBoard) { SysBeep(5); return; }
    ckSide = gHuman;
    Say("Picking a move for you ...");
    if (Think(4, &mv) == 0) return;
    YouMove(&mv);
}

/* ---------- the table list ---------- */

static void LookTables(void)
{
    gView = vwTables;
    if (NetAsk("/tables")) Say(cnTb.n ? "" : "The server listed no tables.");
}

static void SitAt(short i)
{
    if (i < 0 || i >= cnTb.n) return;
    strcpy(cnTable, cnTb.id[i]);
    gView = vwBoard;
    gNet = true;
    gLastMvCnt = -1;
    gNHist = 0;
    memset(&cn, 0, sizeof cn);
    gBoardOk = false;
    NetAsk("/state");
}

static void LeaveTable(void)
{
    if (gNet) NetAsk("/leave");
    gNet = gPollPause = gNetErr = false;
    gView = vwBoard;
    gHuman = CK_BLACK;
    gBoardOk = false;
    NewGame();
}

static void YourName(void)
{
    char t[40];
    if (gNet) { Say("Leave the table first to change your name."); return; }
    strcpy(t, cnName);
    if (AskText("Your name for the checkers server (up to 8 letters and digits):", t, 30)) {
        CnCleanName(t, t);
        if (t[0]) { strcpy(cnName, t); SaveName(); }
    }
    DrawShow();
}

/* ---------- the usual Mac application ---------- */

static void About(void)
{
    DialogPtr d = GetNewDialog(kAboutDlog, NULL, (WindowPtr)-1);
    short item = 0;
    if (!d) return;
    ShowWindow(d);
    InitCursor();
    while (item != 1) ModalDialog(NULL, &item);
    DisposeDialog(d);
    SetPort(gWin);
}

static void Menus(void)                          /* what the Network menu offers now */
{
    MenuHandle m = GetMenuHandle(mNet);
    short i;
    for (i = iLeave; i <= iLook; i++) if (i != 3 && i != 7) { if (gNet) EnableItem(m, i); else DisableItem(m, i); }
    if (gNet) DisableItem(m, iName); else EnableItem(m, iName);
}

static void DoMenu(long mi)
{
    short menu = (short)(mi >> 16), item = (short)mi;
    switch (menu) {
    case mApple:
        if (item == iAbout) About();
        else { Str255 name; GetMenuItemText(GetMenuHandle(mApple), item, name); OpenDeskAcc(name); SetPort(gWin); }
        break;
    case mFile: if (item == iQuit) gQuit = true; break;
    case mEdit: if (!SystemEdit(item - 1) && item == 1) UndoMove(); break;
    case mGame:
        switch (item) {
        case iNew: AskNewGame(); break;
        case iUndo: UndoMove(); break;
        case iDots: gDots = !gDots; DrawShow(); break;
        case iNums: gNums = !gNums; DrawShow(); break;
        case iSides: SwitchSides(); break;
        case iHow: Alert(kHowAlert, NULL); SetPort(gWin); break;
        case iKeys: Alert(kKeysAlert, NULL); SetPort(gWin); break;
        }
        break;
    case mLevel: if (gNet) Say("The level is for games against the Mac."); else SetLevel(item); break;
    case mNet:
        switch (item) {
        case iPlay: if (gNet) LeaveTable(); else LookTables(); break;
        case iLeave: LeaveTable(); break;
        case iDraw: if (gNet) NetAsk("/draw"); break;
        case iResign: if (gNet && Ask("Resign this game?\r\rYour opponent wins it.")) NetAsk("/resign"); break;
        case iAgain: if (gNet) NetAsk("/again"); break;
        case iLook: if (gNet) { gView = vwBoard; NetAsk("/state"); } break;
        case iName: YourName(); break;
        }
        break;
    }
    Menus();
    HiliteMenu(0);
}

static void DoKey(char c)
{
    if (gView == vwTables) {
        if (c >= '1' && c <= '9') SitAt((short)(c - '1'));
        else if (c == 'r' || c == 'R') LookTables();
        else if (c == 'n' || c == 'N') { YourName(); }
        else if (c == 'q' || c == 'Q' || c == 0x1B) { gView = vwBoard; gBoardOk = false; DrawShow(); }
        Menus();
        return;
    }
    if (gNet && gOver && (c == 'a' || c == 'A')) { NetAsk("/again"); return; }
    if (gNet && (c == 'r' || c == 'R')) { NetAsk("/state"); return; }
    switch (c) {
    case '4': case 'j': case 'J': case 0x1C: MoveCursor(0, -1); break;
    case '6': case 'l': case 'L': case 0x1D: MoveCursor(0, 1); break;
    case '8': case 'i': case 'I': case 0x1E: MoveCursor(-1, 0); break;
    case '2': case 'k': case 'K': case 0x1F: MoveCursor(1, 0); break;
    case 0x0D: case 0x03: case ' ': PickSq(ScrSq(gCurR, gCurC)); break;
    case 'n': case 'N': AskNewGame(); break;
    case 'u': case 'U': UndoMove(); break;
    case 'd': case 'D': gDots = !gDots; DrawShow(); break;
    case '#': gNums = !gNums; DrawShow(); break;
    case 's': case 'S': SwitchSides(); break;
    case '1': case '3': if (!gNet) SetLevel((short)(c - '0')); break;   /* 2 and 4 move the cursor */
    case 'h': case 'H': case '?': Alert(kHowAlert, NULL); SetPort(gWin); break;
    case '!': PlayForMe(); break;
    }
}

static void ClickBoard(Point p)
{
    short sr, sc;
    if (gView == vwTables) {
        short i = (short)((p.v - (20 + 64 - 12)) / 16);
        if (p.v >= 20 + 64 - 12 && i >= 0 && i < cnTb.n) SitAt(i);
        Menus();
        return;
    }
    if (p.h < gBX || p.v < gBY || p.h >= gBX + 8 * kCell || p.v >= gBY + 8 * kCell) return;
    sc = (short)((p.h - gBX) / kCell);
    sr = (short)((p.v - gBY) / kCell);
    gCurR = sr; gCurC = sc;
    PickSq(ScrSq(sr, sc));
    if (gNPath < 0) DrawDiff();                  /* the cursor moved to the click */
}

static void DoMouse(EventRecord *e)
{
    WindowPtr w;
    switch (FindWindow(e->where, &w)) {
    case inMenuBar: DoMenu(MenuSelect(e->where)); break;
    case inSysWindow: SystemClick(e, w); break;
    case inDrag: DragWindow(w, e->where, &qd.screenBits.bounds); break;
    case inContent:
        if (w != FrontWindow()) SelectWindow(w);
        else { Point p = e->where; SetPort(w); GlobalToLocal(&p); ckSeed = (ckSeed + p.h * 7 + p.v * 13) % 32749; ClickBoard(p); }
        break;
    }
}

static void Setup(void)
{
    Rect r;
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    FlushEvents(everyEvent, 0);
    gHasWNE = TrapAvailable(0xA860);

    SetMenuBar(GetNewMBar(128));
    AppendResMenu(GetMenuHandle(mApple), 'DRVR');
    DrawMenuBar();

    gWin = GetNewWindow(128, NULL, (WindowPtr)-1);
    r = qd.screenBits.bounds;                    /* a Plus's 512 x 342 or the XL's 608 x 431: fit the screen */
    SizeWindow(gWin, (short)(r.right - r.left - 16), (short)(r.bottom - r.top - 52), true);
    SetPort(gWin);
    TextFont(3);                                 /* Geneva 9 */
    TextSize(9);
    gWinW = gWin->portRect.right; gWinH = gWin->portRect.bottom;
    gBX = 12;
    gBY = (short)((gWinH - 8 * kCell) / 2); if (gBY < 4) gBY = 4;
    gPX = (short)(gBX + 8 * kCell + 14);

    CkInit();
    ckSeed = (long)(TickCount() % 32749);
    ckPoll = ThinkPoll;
    LoadName();
    SetLevel(2);
    Menus();
    NewGame();
}

int main(void)
{
    EventRecord e;
    Setup();
    while (!gQuit) {
        Boolean got = gHasWNE ? WaitNextEvent(everyEvent, &e, 20, NULL) : (SystemTask(), GetNextEvent(everyEvent, &e));
        if (got) switch (e.what) {
        case mouseDown: DoMouse(&e); break;
        case keyDown: case autoKey:
            ckSeed = (ckSeed + (e.message & charCodeMask) + 31 + TickCount() % 997) % 32749;
            if (e.modifiers & cmdKey) DoMenu(MenuKey((char)(e.message & charCodeMask)));
            else if (e.what == keyDown) DoKey((char)(e.message & charCodeMask));
            break;
        case updateEvt:
            BeginUpdate((WindowPtr)e.message);
            if ((WindowPtr)e.message == gWin) { gBoardOk = false; DrawShow(); }
            EndUpdate((WindowPtr)e.message);
            break;
        }
        IdlePoll();
    }
    if (gNet && MbReady()) { char why[200]; CnGet("/leave", why); }   /* stand up, so the seat is free at once */
    MbClose();
    return 0;
}
