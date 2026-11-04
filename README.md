# Checkers XL 0.1d1 (test, 3 Nov 2026)

American checkers on a Macintosh XL (MacWorks Plus or Plus II, System 6 or 7): against the Mac at four levels, or
against people on other Lisas, Macs and FujiNet machines through the checkers table server. The Mac XL counterpart
of LisaCheckers 1.2 (Apple-LisaCheckers), number 4 of the Mac XL library after FujiNet XL; a Lisa and a Mac XL can
play each other.

**Playing:** standard rules: men move diagonally forward; jumps are compulsory and a piece keeps jumping while it
can; a man reaching the far row is crowned and its move ends; a side that cannot move loses; 40 moves each with only
kings moving and nothing taken is a draw. Click a piece, then where it goes (for a jump, where it lands; when two
jump routes end on the same square, each landing square). Dots show where the picked piece can go. Or the arrow keys
(keypad 4 6 8 2, J L I K) and Return. N new game, U undo, D dots, # square numbers, S switch sides, H how to play,
Command-1 to Command-4 the level (Easy, Normal, Hard, Hardest: the Lisa's depths 2, 4, 6, 8 and position budgets).
The computer game needs no drive.

**Network menu:** Play on the Network lists the tables on https://lisa-gw1.tailc2f141.ts.net/checkers (the CK1
protocol of PROTOCOL.md); sit down with the table's number or a click. The server keeps the board and checks every
move; colours swap each game. Offer or Accept a Draw (Command-E), Resign, Another Game (A), Leave the Table, Look at
the Table Again (R), Your Name (letters and digits, 8 at most; kept in "Checkers XL Settings" beside the
application). The requests go through the ESPFuji drive's mailbox ("ESPFuji Mailbox" at the top of a volume, else
MAILBOX.DATA beside the application, as FujiNet XL), opened only when you first use the network. A failed request
shows the drive's own ERROR words. Command-period (or Escape) stops a request; a stopped or unanswered request
pauses the polling until R. Polling as the Lisa: every 3 s at a table, every 15 s while it is your move.

**Memory and speed:** SIZE 160K preferred, 128K minimum (66 KB of code and globals; the network game was run in a
128K partition), for a stock XL (1 MB, 5 MHz).
The search is the Lisa's (same budgets: 400, 1500, 4000, 10000 positions), and each level also has a time limit
(3, 8, 15, 30 s) so that a 5 MHz XL never waits longer; it lets other programs run while it thinks, and
Command-period makes the Mac move at once. **Not yet run on a real XL or on System 6.**

## Build and test
- `bash build.sh` (Retro68) -> build/CheckersXL.bin; `bash tools/package.sh` -> dist/ (800K Disk Copy 4.2 .dsk and
  MacBinary .bin).
- `cc -O2 -I src -o test/work/perft test/perft.c src/rules.c && test/work/perft`: the rules against the published
  perft counts (7 ... 845931), and self-play at each level.
- `bash test/run_qemu.sh` (System 7.6.1 in QEMU, monitor port 45822) with a scenario file: `NOMB=1 NOSIM=1
  SCEN=local` (the computer game with no drive: mouse clicks through tools/qclick.py, which finds the pointer in
  screendumps, then the test key ! that plays your move at level 4, undo, numbers, help, About, switch sides),
  `SCEN=end` (a whole game), `REWRITE=1 SLOW=20 SCEN=cancel` (Command-period and Escape during a slow request),
  `NOMB=1 NOSIM=1 ICOUNT=10 SCEN=speed` (one instruction = 1 us of Mac time, about a 10 MHz 68000: level 1 took
  0.1 s, level 2 2.7 s (824 positions), level 3 and 4 stopped at their time limits; so about 150 positions a second
  on a 5 MHz XL, where levels 2-4 are bounded by time more often than by the Lisa's budgets).
- `bash test/net_test.sh`: a network game against a LOCAL copy of the table server (port 8090; fujisim's `--rewrite`
  sends the public address there), the other seat played with curl (test/opponent.sh): sit down, moves by clicks and
  by !, a draw offered and accepted, another game with the colours swapped, the opponent resigns, leave, and the
  server stopped to show the drive's ERROR words. Never on Tim's public tables.
- `src/mailbox.c`, `src/xlproto.c` and `tools/fujisim.py` are FujiNet XL's, unchanged.

Not supported; Tim O'Connor's lab, 2026.
