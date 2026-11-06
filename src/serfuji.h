/* serfuji.h: an RS-232 FujiNet on the Macintosh XL's serial port (FujiBus, as RS-232 FujiNet firmware 1.6 speaks it),
 * the second route beside the ESPFuji mailbox.  Modelled on LisaFuji 1.2's FnBus unit (tested on the Lisa 2/10):
 *   frame   C0 packet C0; inside, C0 goes as DB DC and DB as DB DD
 *   packet  device, command, length (low, high), checksum, descriptor, parameters, payload
 * Every request gets one reply, ACK (06) or NAK (15).  Under MacWorks the XL's two serial ports are the Mac's
 * modem port (.AIn/.AOut) and printer port (.BIn/.BOut).  6 Nov 2026. */
#ifndef SERFUJI_H
#define SERFUJI_H

#define SF_MODEM    0
#define SF_PRINTER  1

/* Open the port and find the FujiNet: 19200 baud, then 9600 (the speed that answered last time first).
 * noErr: it answered; where gets "Serial: FujiNet on Serial A, 19200 baud."  Else the port is closed again and
 * where says why (port in use by AppleTalk, no answer). */
OSErr SfOpen(short port, long *baud, char *where);
void SfClose(void);
Boolean SfUp(void);
void SfRouteText(char *out);                /* "the FujiNet on Serial A, 19200 baud" */

/* The mailbox requests the applications send, answered over FujiBus with the drive's words:
 *   HELLO  -> "FujiNet <firmware> on Serial A, 19200 baud: network <ssid>, host <name>, address a.b.c.d"
 *   WIFI   -> "ssid <ssid> status connected ip a.b.c.d" (or "status disconnected")
 *   CLOCK  -> "OK yyyy mm dd hh mi ss"
 *   OPEN <url> -> "OK <bytes> <http code>" or "ERROR ...";  READ <offset> <count> (in order) -> the bytes; echo = offset
 *   CLOSE  -> closes network unit N1
 * Results as MbTransact: noErr, userCanceledErr, 1 (no answer), 2 (anything else; out holds the words). */
OSErr SfTransact(const char *cmd, unsigned char *out, short *outlen, unsigned long *echo);

/* GET a page as MbFetch does: N1 OPEN (mode 4), STATUS, READs of up to 256 bytes, CLOSE. */
OSErr SfFetch(const char *url, char *buf, long max, long *got, long *total, int *code, char *why);

#endif
