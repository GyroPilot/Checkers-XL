/* ckxl.r: Checkers XL's menus, alerts and dialogs.  Everything fits a 512 x 342 screen (and the XL's 608 x 431).
 * Static texts stay under 255 characters each (a DITL item's limit); long texts are in several items. */
#include "Multiverse.r"
#include "Processes.r"

resource 'MBAR' (128) { { 128; 129; 130; 131; 132; 133 } };

resource 'MENU' (128) { 128, textMenuProc, 0x7FFFFFFD, enabled, apple, {
    "About Checkers XL\0xC9", noIcon, noKey, noMark, plain;
    "-", noIcon, noKey, noMark, plain
} };
resource 'MENU' (129) { 129, textMenuProc, 0x7FFFFFFF, enabled, "File", { "Quit", noIcon, "Q", noMark, plain } };
resource 'MENU' (130) { 130, textMenuProc, 0x7FFFFFFD, enabled, "Edit", {
    "Undo", noIcon, "Z", noMark, plain; "-", noIcon, noKey, noMark, plain;
    "Cut", noIcon, "X", noMark, plain; "Copy", noIcon, "C", noMark, plain;
    "Paste", noIcon, "V", noMark, plain; "Clear", noIcon, noKey, noMark, plain
} };
/* the Lisa's Game menu, with its Help menu's two items at the bottom */
resource 'MENU' (131) { 131, textMenuProc, 0x7FFFFFBB, enabled, "Game", {
    "New Game", noIcon, "N", noMark, plain;
    "Undo", noIcon, "U", noMark, plain;
    "-", noIcon, noKey, noMark, plain;
    "Show or Hide the Moves", noIcon, "D", noMark, plain;
    "Show or Hide Square Numbers", noIcon, noKey, noMark, plain;
    "Switch Sides", noIcon, "S", noMark, plain;
    "-", noIcon, noKey, noMark, plain;
    "How to Play", noIcon, "H", noMark, plain;
    "Keys", noIcon, noKey, noMark, plain
} };
resource 'MENU' (132) { 132, textMenuProc, 0x7FFFFFFF, enabled, "Level", {
    "1  Easy", noIcon, "1", noMark, plain;
    "2  Normal", noIcon, "2", noMark, plain;
    "3  Hard", noIcon, "3", noMark, plain;
    "4  Hardest", noIcon, "4", noMark, plain
} };
resource 'MENU' (133) { 133, textMenuProc, 0x7FFFFFBB, enabled, "Network", {
    "Play on the Network\0xC9", noIcon, "T", noMark, plain;
    "Leave the Table", noIcon, "L", noMark, plain;
    "-", noIcon, noKey, noMark, plain;
    "Offer or Accept a Draw", noIcon, "E", noMark, plain;
    "Resign\0xC9", noIcon, noKey, noMark, plain;
    "Another Game", noIcon, "A", noMark, plain;
    "-", noIcon, noKey, noMark, plain;
    "Look at the Table Again", noIcon, "R", noMark, plain;
    "Your Name\0xC9", noIcon, noKey, noMark, plain
} };

resource 'WIND' (128) { { 40, 8, 330, 504 }, documentProc, visible, noGoAway, 0, "Checkers XL", noAutoCenter };

/* a line of text: your name */
resource 'DLOG' (128) { { 70, 20, 186, 492 }, dBoxProc, invisible, noGoAway, 0, 128, "", centerMainScreen };
resource 'DITL' (128) { {
    { 84, 380, 104, 452 }, Button { enabled, "OK" };
    { 84, 296, 104, 368 }, Button { enabled, "Cancel" };
    { 10, 14, 28, 452 }, StaticText { disabled, "" };
    { 36, 16, 54, 450 }, EditText { enabled, "" }
} };

/* About: the Lisa's About alert, and the credits of its README */
resource 'DLOG' (129) { { 50, 26, 280, 486 }, dBoxProc, invisible, noGoAway, 0, 129, "", centerMainScreen };
resource 'DITL' (129) { {
    { 198, 370, 218, 440 }, Button { enabled, "OK" };
    { 10, 14, 30, 446 }, StaticText { disabled, "Checkers XL 1.1d1 serial test   (6 November 2026)" };
    { 34, 14, 118, 446 }, StaticText { disabled,
        "American checkers on a Macintosh XL: you against the Mac at four levels, or people on other "
        "Lisas, Macs and FujiNet machines through the checkers server (the network game goes through the "
        "ESPFuji drive). The Mac XL counterpart of LisaCheckers 1.2." };
    { 124, 14, 160, 446 }, StaticText { disabled,
        "Tim O'Connor (bmwcyclist on LisaList2, GyroPilot on GitHub). LisaCheckers: Lisa Pascal, Lisa "
        "Workshop 3.0, tool 419317." };
    { 166, 14, 186, 446 }, StaticText { disabled, "By GyroPilot (Tim O'Connor), 2026." }
} };

/* a message: ^0 (game over, and the like) */
resource 'ALRT' (130) { { 80, 56, 214, 456 }, 130, { OK, visible, silent; OK, visible, silent; OK, visible, silent; OK, visible, silent }, centerMainScreen };
resource 'DITL' (130) { {
    { 102, 310, 122, 380 }, Button { enabled, "OK" };
    { 10, 64, 94, 384 }, StaticText { disabled, "^0" };
    { 10, 16, 42, 48 }, Icon { disabled, 1 }
} };

/* a question: ^0, OK or Cancel */
resource 'ALRT' (131) { { 80, 56, 214, 456 }, 131, { OK, visible, silent; OK, visible, silent; OK, visible, silent; OK, visible, silent }, centerMainScreen };
resource 'DITL' (131) { {
    { 102, 310, 122, 380 }, Button { enabled, "OK" };
    { 102, 226, 122, 296 }, Button { enabled, "Cancel" };
    { 10, 64, 94, 384 }, StaticText { disabled, "^0" };
    { 10, 16, 42, 48 }, Icon { disabled, 2 }
} };

/* How to Play (the Lisa's alert 16) */
resource 'ALRT' (132) { { 40, 16, 300, 496 }, 132, { OK, visible, silent; OK, visible, silent; OK, visible, silent; OK, visible, silent }, centerMainScreen };
resource 'DITL' (132) { {
    { 230, 396, 250, 466 }, Button { enabled, "OK" };
    { 8, 14, 26, 466 }, StaticText { disabled, "How to play" };
    { 28, 14, 94, 466 }, StaticText { disabled,
        "Pieces move one square diagonally forward onto an empty dark square. A piece jumps an enemy piece "
        "next to it when the square beyond is empty, and takes it. Jumps must be taken, and a piece keeps "
        "jumping while it can." };
    { 100, 14, 134, 466 }, StaticText { disabled,
        "A piece that reaches the far row becomes a king (a ring in the middle) and may then move and jump "
        "backward too." };
    { 140, 14, 174, 466 }, StaticText { disabled,
        "Click your piece, then the square it goes to; for a jump, where it lands. Dots show where it can go." };
    { 180, 14, 214, 466 }, StaticText { disabled,
        "You win when the other side cannot move: all its pieces taken or blocked." }
} };

/* Keys (the Lisa's alert 17) */
resource 'ALRT' (133) { { 26, 36, 334, 476 }, 133, { OK, visible, silent; OK, visible, silent; OK, visible, silent; OK, visible, silent }, centerMainScreen };
resource 'DITL' (133) { {
    { 280, 356, 300, 426 }, Button { enabled, "OK" };
    /* 0.1d2: returns (\r) for line breaks (\n shows as a box), and three pieces under 255 characters each */
    { 8, 14, 106, 426 }, StaticText { disabled,
        "Keys\r"
        "Mouse ... click a piece, then where it goes\r"
        "Arrows, 4 6 8 2 (keypad) or J L I K ... the cursor\r"
        "Return or Space ... pick the cursor square\r"
        "N ... new game (Apple-N)\r"
        "U ... take back your last move (Apple-U)" };
    { 108, 14, 174, 426 }, StaticText { disabled,
        "D ... show or hide the dots (Apple-D)\r"
        "# ... show or hide the square numbers\r"
        "S ... switch sides: new game, the Mac opens\r"
        "Apple-1 to Apple-4 ... the Mac's level" };
    { 176, 14, 276, 426 }, StaticText { disabled,
        "H ... How to Play (Apple-H)\r"
        "At a table: A another game, R look again.\r"
        "Apple-period ... stop (or: move now)" }
} };

resource 'vers' (1) { 0x01, 0x10, development, 0x01, verUS, "1.1d1", "1.1d1 serial test, Checkers XL, Macintosh XL / MacWorks" };

/* a stock XL: 1 MB.  The application wants 160K: 66 KB of code and globals (the search 13 KB), the rest heap and stack. */
resource 'SIZE' (-1) {
    reserved, acceptSuspendResumeEvents, reserved, canBackground, doesActivateOnFGSwitch,
    backgroundAndForeground, dontGetFrontClicks, ignoreChildDiedEvents, is32BitCompatible,
    notHighLevelEventAware, onlyLocalHLEvents, notStationeryAware, dontUseTextEditServices,
    reserved, reserved, reserved,
    160 * 1024,
    128 * 1024
};

#include "icons.r"
data 'CKXL' (0, "Owner resource") { $"0B436865 636B6572 7320584C" };   /* "Checkers XL" */
resource 'FREF' (128) { 'APPL', 0, "" };
resource 'BNDL' (128) { 'CKXL', 0, { 'ICN#', { 0, 128 }; 'FREF', { 0, 128 } } };
