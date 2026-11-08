# Checkers XL 1.1 (7 November 2026)

American checkers on a Macintosh XL (MacWorks Plus or Plus II, System 6 or 7): against the Mac at four levels, or
against people on other Lisas, Macs and FujiNet machines through the checkers table server. The Mac XL counterpart
of LisaCheckers 1.2 (Apple-LisaCheckers), number 4 of the Mac XL library after FujiNet XL; a Lisa and a Mac XL can
play each other.

**Playing:** standard rules: men move diagonally forward; jumps are compulsory and a piece keeps jumping while it
can; a man reaching the far row is crowned and its move ends; a side that cannot move loses; 40 moves each with only
kings moving and nothing taken is a draw. Click a piece, then where it goes (for a jump, where it lands; when two
jump routes end on the same square, each landing square). Dots show where the picked piece can go. Or the arrow keys
(keypad 4 6 8 2, J L I K) and Return. N new game, U undo, D dots, # square numbers, S switch sides, H how to play,
Apple-1 to Apple-4 the level (Easy, Normal, Hard, Hardest: the Lisa's depths 2, 4, 6, 8 and position budgets).
The computer game needs no drive.

**Network menu:** Play on the Network lists the tables on https://lisa-gw1.tailc2f141.ts.net/checkers (the CK1
protocol of PROTOCOL.md); sit down with the table's number or a click. The server keeps the board and checks every
move; colours swap each game. Offer or Accept a Draw (Apple-E), Resign, Another Game (A), Leave the Table, Look at
the Table Again (R), Your Name (letters and digits, 8 at most; kept in "Checkers XL Settings" beside the
application). The requests go through the ESPFuji drive's mailbox ("ESPFuji Mailbox" at the top of a volume, else
MAILBOX.DATA beside the application, as FujiNet XL), opened only when you first use the network. A failed request
shows the drive's own ERROR words. Apple-period (or Escape) stops a request; a stopped or unanswered request
pauses the polling until R. Polling as the Lisa: every 3 s at a table, every 15 s while it is your move.

**Memory and speed:** SIZE 160K preferred, 128K minimum (66 KB of code and globals; the network game was run in a
128K partition), for a stock XL (1 MB, 5 MHz).
The search is the Lisa's (same budgets: 400, 1500, 4000, 10000 positions), and each level also has a time limit
(3, 8, 15, 30 s) so that a 5 MHz XL never waits longer; it lets other programs run while it thinks, and
Apple-period makes the Mac move at once. Runs on System 6.0.2 and later.

## New in 1.1: a serial FujiNet route
Besides the ESProFile, Checkers XL can talk to an RS-232 FujiNet on Serial A or Serial B.
- **Firmware:** FujiNet v1.6.1 or later (the nightly n2026.301 crashes on HTTPS pages and never answers).
- **Cable:** the Lisa FujiNet cable used with LisaFuji (straight-through, never a null modem).
- **Speed:** set the FujiNet to 19200 (or 9600) baud on its own web page; it comes set to 115200, which MacWorks
  cannot run. Checkers XL tries 19200, then 9600.
- **System 6:** under MacWorks on System 6, a file named "AppleTalk" in the System Folder (AppleTalk 57, from an
  Ethernet installer) makes every program that opens a serial port bomb. Checkers XL checks for it and says so; move it
  out and restart. System 7.5.5 is fine.
- **Menu:** the last items of the Network menu choose the route: Automatic Route (the ESProFile if it answers, else
  Serial A, then Serial B), Use the ESProFile, Use Serial A, Use Serial B, and Make a New Mailbox (a fresh
  MAILBOX.DATA, for a damaged one). The choice is kept in "XL FujiNet Settings" in the System Folder and shared by
  all the Mac XL apps.

**Tested** on a real Macintosh XL (MacWorks Plus II, System 7.5.5): the computer game and a network game, on a Lisa 2/10 (MacWorks Plus II,
System 6.0.8) through the ESProFile and with a real RS-232 FujiNet on Serial B, and in emulators on System 6.0.2 to
7.5.5. The release (an 800K Disk Copy 4.2 image and a MacBinary file) is under Releases.

## Build and test
- `bash build.sh` (Retro68) -> build/CheckersXL.bin; `bash tools/package.sh` -> dist/ (800K Disk Copy 4.2 .dsk and
  MacBinary .bin).
- `cc -O2 -I src -o test/work/perft test/perft.c src/rules.c && test/work/perft`: the rules against the published
  perft counts (7 ... 845931), and self-play at each level.
- `bash test/run_qemu.sh` (System 7.6.1 in QEMU, monitor port 45822) with a scenario file: `NOMB=1 NOSIM=1
  SCEN=local` (the computer game with no drive: mouse clicks through tools/qclick.py, which finds the pointer in
  screendumps, then the test key ! that plays your move at level 4, undo, numbers, help, About, switch sides),
  `SCEN=end` (a whole game), `REWRITE=1 SLOW=20 SCEN=cancel` (Apple-period and Escape during a slow request),
  `NOMB=1 NOSIM=1 ICOUNT=10 SCEN=speed` (one instruction = 1 us of Mac time, about a 10 MHz 68000: level 1 took
  0.1 s, level 2 2.7 s (824 positions), level 3 and 4 stopped at their time limits; so about 150 positions a second
  on a 5 MHz XL, where levels 2-4 are bounded by time more often than by the Lisa's budgets).
- `bash test/net_test.sh`: a network game against a LOCAL copy of the table server (port 8090; fujisim's `--rewrite`
  sends the public address there), the other seat played with curl (test/opponent.sh): sit down, moves by clicks and
  by !, a draw offered and accepted, another game with the colours swapped, the opponent resigns, leave, and the
  server stopped to show the drive's ERROR words. Never on Tim's public tables.
- `src/mailbox.c`, `src/xlproto.c` and `tools/fujisim.py` are FujiNet XL's, unchanged.

Not supported; Tim O'Connor's lab, 2026.
