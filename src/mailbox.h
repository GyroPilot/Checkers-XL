/* mailbox.h: the ESPFuji drive, seen from a Macintosh XL application (mailbox protocol version 2, xlproto.h).
 * Shared by the Mac XL library (FujiNet XL first).  Firmware 1.1b2 or later; 1.1b1 works with the offset-0 allowance. */
#ifndef MAILBOX_H
#define MAILBOX_H

#define MB_PAGES     64                     /* the mailbox file: 64 pages of 512 bytes in one run */
#define MB_WAIT      (45L * 60)             /* ticks: an OPEN may take the drive 30 s; give it 45 */
#define MB_BODY_MAX  32768L                 /* the drive keeps at most 32 KB of a page */

/* Find or make the mailbox: "ESPFuji Mailbox" at the top of any mounted volume (as the Lisa tools), else
 * MAILBOX.DATA beside this application (made in one piece).  where gets a line saying which. */
OSErr MbOpen(char *where);
void MbClose(void);
Boolean MbReady(void);

/* One request, one reply (up to 492 bytes in out, not terminated).  echo: the reply's bytes 16-19.
 * noErr, userCanceledErr (Command-period), or 1 (no answer in 45 s). */
OSErr MbTransact(const char *cmd, unsigned char *out, short *outlen, unsigned long *echo);

/* GET a page: OPEN, READ in pieces into buf (up to max bytes, terminated), CLOSE.  On a drive error, why gets its
 * words ("ERROR ...") and the result is 2.  total and code: the drive's "OK <bytes> <http code>". */
OSErr MbFetch(const char *url, char *buf, long max, long *got, long *total, int *code, char *why);

/* A line for a failed request. */
void MbErrorText(const char *what, OSErr err, const char *why, char *out);

/* Called while waiting: return true to stop (Command-period).  The application supplies it. */
Boolean MbCancelPressed(void);
void MbBreathe(void);
/* Idle up to ticks, as a waiting program should (WaitNextEvent with a sleep); true when the user stopped it. */
Boolean MbIdle(short ticks);

/* The route (6 Nov 2026): the ESPFuji mailbox or an RS-232 FujiNet on a serial port (serfuji.c).  MbOpen picks it from
 * the choice kept in "XL FujiNet Settings" (System Folder), shared by every Mac XL application. */
#define MB_AUTO       0
#define MB_ESPROFILE  1
#define MB_MODEM      2
#define MB_PRINTER    3
void MbRouteText(char *out);                         /* "the FujiNet on the Modem Port, 19200 baud" */
void MbRouteMenu(MenuHandle m);                      /* append the route items to a menu (the Network menu) */
Boolean MbRouteHit(MenuHandle m, short item, char *where);   /* a route item chosen: reopen; where gets the line */

#endif
