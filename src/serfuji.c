/* serfuji.c: see serfuji.h.  Byte order on the wire is low byte first; every 16-bit value is put on and taken off
 * one byte at a time.  Rules from LisaFuji's FnBus, kept here: stale input is thrown away before each command; a
 * network READ is never repeated and never asks for more than 256 bytes or more than STATUS said is waiting; OPEN is
 * always followed by STATUS (OPEN is answered ACK even for a dead server); a CLOSE answered NAK still closed. */
#include <Multiverse.h>
#include <string.h>
#include <stdio.h>
#include "mailbox.h"
#include "serfuji.h"

#define DEV_FUJI   0x70
#define DEV_N1     0x71
#define DEV_CLOCK  0x45
#define ACK        0x06
#define NAK        0x15

#define T_REPLY    (5 * 60)        /* ticks: first byte of a reply */
#define T_PROBE    90              /* the same while looking for the speed */
#define T_OPEN     (25 * 60)       /* OPEN and the STATUS after it: outlast the board's own 15 s HTTP wait */
#define T_GAP      (2 * 60)        /* longest silence inside a reply */
#define T_DRAIN    (15 * 60)       /* a page that stops arriving */

#define R_OK       0
#define R_NAK      3               /* internal codes; turned into MbTransact's before they leave this file */
#define R_NOREP    1
#define R_BAD      4
#define R_STALL    5
#define R_PORT     6

static short sIn = 0, sOut = 0, sPort = SF_MODEM;
static long sBaud = 0;
static Boolean sUp = false, sOpen = false;
static unsigned char sPkt[600];    /* the last packet, not escaped */
static unsigned char sWire[560];   /* a frame going out, or bytes coming in */
static char sInBuf[4096] __attribute__((aligned(2)));   /* the driver's input buffer: the default 64 bytes is too small */
static short sLen, sOff, sPl;      /* the last reply: packet length, where its data starts, data bytes */
static unsigned char sCmd;         /* its command byte */
static long sWait;                 /* N1: bytes waiting, from the last STATUS */
static short sConn, sStat;         /* N1: connected flag and status code from the last STATUS */

Boolean SfUp(void) { return sUp; }

static const char *PortName(short p) { return p == SF_PRINTER ? "Serial B" : "Serial A"; }   /* the ports' own names (Tim, 6 Nov):
                                              * under MacWorks Serial A is the Mac modem port (.A), Serial B the printer port (.B) */

void SfRouteText(char *out) { sprintf(out, "the FujiNet on %s, %ld baud", PortName(sPort), sBaud); }

static unsigned char Chk(short n)
{
    unsigned short c = 0;
    short i;
    for (i = 0; i < n; i++) { c += sPkt[i]; c = (c >> 8) + (c & 0xFF); }
    return (unsigned char)c;
}

static short DescSize(unsigned char d)
{
    short k = d & 7;
    return k <= 4 ? k : (k == 5 ? 2 : 4);
}

static void Flush(void)
{
    long n = 0;
    while (SerGetBuf(sIn, &n) == noErr && n > 0) {
        if (n > (long)sizeof sWire) n = sizeof sWire;
        FSRead(sIn, &n, sWire);
    }
}

/* desc 0: no parameters; 1: p0 as one byte; 2: p0 and p1 as one byte each; 5: p0 as 16 bits, low byte first. */
static short Send(unsigned char dev, unsigned char cmd, unsigned char desc, unsigned short p0, unsigned char p1,
                  const unsigned char *pl, short plLen)
{
    short n = 6, i, w = 0;
    long count;
    sPkt[0] = dev; sPkt[1] = cmd; sPkt[4] = 0; sPkt[5] = desc;
    if (desc == 1) sPkt[n++] = (unsigned char)p0;
    else if (desc == 2) { sPkt[n++] = (unsigned char)p0; sPkt[n++] = p1; }
    else if (desc == 5) { sPkt[n++] = (unsigned char)(p0 & 0xFF); sPkt[n++] = (unsigned char)(p0 >> 8); }
    if (plLen > 256) return R_PORT;
    for (i = 0; i < plLen; i++) sPkt[n++] = pl[i];
    sPkt[2] = (unsigned char)(n & 0xFF); sPkt[3] = (unsigned char)(n >> 8);
    sPkt[4] = Chk(n);
    sWire[w++] = 0xC0;
    for (i = 0; i < n; i++) {
        if (sPkt[i] == 0xC0) { sWire[w++] = 0xDB; sWire[w++] = 0xDC; }
        else if (sPkt[i] == 0xDB) { sWire[w++] = 0xDB; sWire[w++] = 0xDD; }
        else sWire[w++] = sPkt[i];
    }
    sWire[w++] = 0xC0;
    count = w;
    if (FSWrite(sOut, &count, sWire) != noErr || count != w) return R_PORT;
    return R_OK;
}

/* One frame into sPkt.  Bytes before the opening C0 are ignored, and so is a C0 after fewer bytes than any packet
 * has (it opens a frame instead of ending one).  userCanceledErr on Apple-period. */
static short Recv(long firstTicks)
{
    unsigned long t0 = TickCount();
    long limit = firstTicks, avail, got;
    short n = 0, i, off, par, len;
    Boolean inFr = false, esc = false, done = false, over = false;
    unsigned char d;

    sLen = 0; sOff = 6; sPl = 0; sCmd = 0;
    while (!done) {
        avail = 0;
        if (SerGetBuf(sIn, &avail) != noErr) return R_PORT;
        if (avail > 0) {
            got = avail > 128 ? 128 : avail;
            if (FSRead(sIn, &got, sWire) != noErr && got <= 0) return R_PORT;
            for (i = 0; i < got && !done; i++) {
                unsigned char b = sWire[i];
                if (b == 0xC0) {
                    if (inFr && n >= 6) done = true;
                    else { inFr = true; esc = false; n = 0; }
                } else if (inFr) {
                    if (esc) { esc = false; b = b == 0xDC ? 0xC0 : (b == 0xDD ? 0xDB : b); }
                    else if (b == 0xDB) { esc = true; continue; }
                    if (n < (short)sizeof sPkt) sPkt[n++] = b; else over = true;
                }
            }
            t0 = TickCount(); limit = T_GAP;
        } else {
            if ((long)(TickCount() - t0) > limit) return inFr ? R_STALL : R_NOREP;
            if (MbIdle(1)) return userCanceledErr;
        }
    }
    sLen = n;
    if (over || n < 6) return R_BAD;
    len = sPkt[2] | (sPkt[3] << 8);
    if (len != n) return R_BAD;
    d = sPkt[4]; sPkt[4] = 0;
    if (Chk(n) != d) { sPkt[4] = d; return R_BAD; }
    sPkt[4] = d;
    d = sPkt[5]; off = 6; par = DescSize(d);
    while ((d & 0x80) && off < n) { d = sPkt[off++]; par += DescSize(d); }
    off += par;
    if (off > n) return R_BAD;
    sCmd = sPkt[1]; sOff = off; sPl = n - off;
    return R_OK;
}

static short Call(unsigned char dev, unsigned char cmd, unsigned char desc, unsigned short p0, unsigned char p1,
                  const unsigned char *pl, short plLen, long firstTicks)
{
    short rc;
    if (!sUp) return R_PORT;
    Flush();
    if ((rc = Send(dev, cmd, desc, p0, p1, pl, plLen)) != R_OK) return rc;
    if ((rc = Recv(firstTicks)) != R_OK) return rc;
    if (sCmd == NAK) return R_NAK;
    return sCmd == ACK ? R_OK : R_BAD;
}

static short Ready(long t)
{
    short rc = Call(DEV_FUJI, 0x00, 0, 0, 0, NULL, 0, t), i;
    if (rc != R_OK) return rc;
    if (sPl != 512) return R_BAD;
    for (i = 0; i < 512; i++) if (sPkt[sOff + i] != 'A') return R_BAD;
    return R_OK;
}

static void Kill(short ref)                       /* KillIO: Retro68 has no glue for it */
{
    ParamBlockRec pb;
    memset(&pb, 0, sizeof pb);
    pb.ioParam.ioRefNum = ref;
    PBKillIOSync(&pb);
}

static void SfClosePort(void)
{
    if (sIn) { SerSetBuf(sIn, NULL, 0); Kill(sIn); CloseDriver(sIn); }
    if (sOut) { Kill(sOut); CloseDriver(sOut); }
    sIn = sOut = 0; sUp = false; sOpen = false;
}

void SfClose(void)
{
    if (sUp && sOpen) Call(DEV_N1, 0x43, 0, 0, 0, NULL, 0, T_REPLY);
    SfClosePort();
}

static OSErr SetSpeed(long baud)
{
    SerShk shk;
    short cfg = (baud == 19200 ? baud19200 : baud9600) + data8 + stop10 + noParity;
    OSErr err;
    memset(&shk, 0, sizeof shk);                 /* no handshake, no XON/XOFF: FujiBus frames carry any byte */
    if ((err = SerReset(sOut, cfg)) != noErr) return err;
    SerReset(sIn, cfg);
    SerHShake(sOut, &shk);
    SerHShake(sIn, &shk);
    sBaud = baud;
    return noErr;
}

OSErr SfOpen(short port, long *baud, char *where)
{
    static const unsigned char modemIn[] = "\p.AIn", modemOut[] = "\p.AOut", prIn[] = "\p.BIn", prOut[] = "\p.BOut";
    long tries[2];
    short i, rc = R_NOREP;
    OSErr err;

    SfClose();
    sPort = port;
    err = OpenDriver(port == SF_PRINTER ? prOut : modemOut, &sOut);
    if (err == noErr) err = OpenDriver(port == SF_PRINTER ? prIn : modemIn, &sIn);
    if (err) {
        sIn = sOut = 0;
        if (port == SF_PRINTER)
            sprintf(where, "Serial: Serial B would not open (error %d). Is AppleTalk set to LocalTalk in the Network control panel? It holds Serial B then.", (int)err);
        else sprintf(where, "Serial: Serial A would not open (error %d).", (int)err);
        return err;
    }
    SerSetBuf(sIn, sInBuf, sizeof sInBuf);
    sUp = true;
    tries[0] = (*baud == 9600) ? 9600 : 19200;
    tries[1] = tries[0] == 9600 ? 19200 : 9600;
    for (i = 0; i < 2; i++) {
        if ((err = SetSpeed(tries[i])) != noErr) break;
        rc = Ready(T_PROBE);
        if (rc == R_OK || rc == userCanceledErr) break;
    }
    if (err == noErr && rc == R_OK) {
        *baud = sBaud;
        sprintf(where, "Serial: the FujiNet on %s, %ld baud.", PortName(port), sBaud);
        return noErr;
    }
    SfClosePort();
    if (rc == userCanceledErr) { sprintf(where, "Serial: looking for the FujiNet on %s was stopped.", PortName(port)); return userCanceledErr; }
    sprintf(where, "Serial: no FujiNet answered on %s at 19200 or 9600 baud.", PortName(port));
    return 1;
}

static OSErr Result(short rc, char *words, const char *what)
{
    switch (rc) {
    case R_OK: return noErr;
    case userCanceledErr: return userCanceledErr;
    case R_NOREP: return 1;
    case R_NAK: sprintf(words, "the FujiNet refused %s", what); return 2;
    case R_STALL: sprintf(words, "the FujiNet's answer to %s stopped part-way", what); return 2;
    case R_BAD: sprintf(words, "the FujiNet's answer to %s was damaged (wrong speed?)", what); return 2;
    default: sprintf(words, "the serial port failed during %s", what); return 2;
    }
}

static void CfgStr(short off, short max, char *out)
{
    short i;
    for (i = 0; i < max - 1 && off + i < sPl; i++) {
        unsigned char c = sPkt[sOff + off + i];
        if (!c) break;
        out[i] = (c < 32 || c > 126) ? '.' : (char)c;
    }
    out[i] = 0;
}

static short SfStatus(long t);
static long sDelivered;                    /* OPEN/READ requests: bytes of the open page handed over so far */

/* The drive's OPEN: N1 OPEN, then STATUS until the FujiNet holds the whole page (status 136 after it, or the count
 * stops growing for a second), so "OK <bytes> <code>" can give the size as the drive does. */
static OSErr OpenAsDrive(const char *url, char *s)
{
    unsigned char spec[256];
    short rc, n = (short)strlen(url);
    long last = -1;
    unsigned long still = 0, from = TickCount();
    sDelivered = 0;
    if (n + 4 > 256) { strcpy(s, "ERROR the address is too long for a serial FujiNet (252 characters at most)"); return noErr; }
    if (sOpen) { Call(DEV_N1, 0x43, 0, 0, 0, NULL, 0, T_REPLY); sOpen = false; }
    memcpy(spec, "N1:", 3); memcpy(spec + 3, url, n); spec[n + 3] = 0;
    rc = Call(DEV_N1, 0x4F, 2, 4, 0, spec, (short)(n + 4), T_OPEN);
    if (rc == R_NAK) { strcpy(s, "ERROR the FujiNet refused the address"); return noErr; }
    if (rc != R_OK) return Result(rc, s, "OPEN");
    sOpen = true;
    if ((rc = SfStatus(T_OPEN)) != R_OK) return Result(rc, s, "STATUS");
    if (sStat == 207) { strcpy(s, "ERROR no connection: the FujiNet could not reach the server"); return noErr; }
    if (sStat != 1 && sStat != 136 && sStat != 170) { sprintf(s, "ERROR the FujiNet said status %d", sStat); return noErr; }
    while (sStat == 1 && TickCount() - from < T_DRAIN) {
        if (sWait != last) { last = sWait; still = TickCount(); }
        else if (TickCount() - still > 60) break;
        if (MbIdle(10)) return userCanceledErr;
        if ((rc = SfStatus(T_REPLY)) != R_OK) return Result(rc, s, "STATUS");
    }
    sprintf(s, "OK %ld %d", sWait, sStat == 170 ? 404 : 200);
    return noErr;
}

/* The drive's READ <offset> <count>, in order only: up to count bytes (two FujiBus READs of at most 256). */
static OSErr ReadAsDrive(const char *cmd, unsigned char *out, short *outlen, unsigned long *echo)
{
    unsigned long off = 0;
    int want = 0;
    short got = 0, rc;
    if (sscanf(cmd, "READ %lu %d", &off, &want) != 2 || want < 1) { strcpy((char *)out, "ERROR bad READ"); *outlen = 14; return 2; }
    if (!sOpen || (long)off != sDelivered) { strcpy((char *)out, "ERROR out of step"); *outlen = 17; return 2; }
    if (echo) *echo = off;
    if (want > 492) want = 492;
    while (got < want && sWait > 0) {
        short n = (short)(want - got);
        if (n > 256) n = 256;
        if (n > sWait) n = (short)sWait;
        if ((rc = Call(DEV_N1, 0x52, 5, (unsigned short)n, 0, NULL, 0, T_REPLY)) != R_OK) { *outlen = 0; return Result(rc, (char *)out, "READ"); }
        if (sPl < 1 || sPl > n) { strcpy((char *)out, "ERROR wrong byte count"); *outlen = 22; return 2; }
        memcpy(out + got, sPkt + sOff, sPl);
        got += sPl; sWait -= sPl;
    }
    sDelivered += got;
    *outlen = got;
    return noErr;
}

OSErr SfTransact(const char *cmd, unsigned char *out, short *outlen, unsigned long *echo)
{
    char *s = (char *)out, ssid[34], host[65], fw[16];
    short rc;
    OSErr err;

    *outlen = 0; s[0] = 0;
    if (echo) *echo = 0;
    if (!sUp) return fnOpnErr;
    if (strcmp(cmd, "HELLO") == 0) {
        if ((err = Result(rc = Ready(T_REPLY), s, "HELLO")) != noErr) { *outlen = strlen(s); return err; }
        rc = Call(DEV_FUJI, 0xE8, 0, 0, 0, NULL, 0, T_REPLY);
        if (rc == R_OK && sPl >= 140) {
            const unsigned char *a = sPkt + sOff + 97;
            CfgStr(0, 33, ssid); CfgStr(33, 64, host); CfgStr(125, 15, fw);
            sprintf(s, "FujiNet %s on %s, %ld baud: network %s, host %s, address %d.%d.%d.%d",
                    fw, PortName(sPort), sBaud, ssid, host, a[0], a[1], a[2], a[3]);
        } else sprintf(s, "FujiNet on %s, %ld baud: ready", PortName(sPort), sBaud);
    } else if (strcmp(cmd, "WIFI") == 0) {
        short st;
        if ((err = Result(Call(DEV_FUJI, 0xFA, 0, 0, 0, NULL, 0, T_REPLY), s, "WIFI")) != noErr) { *outlen = strlen(s); return err; }
        st = sPl > 0 ? sPkt[sOff] : 0;
        rc = Call(DEV_FUJI, 0xE8, 0, 0, 0, NULL, 0, T_REPLY);
        if (rc == R_OK && sPl >= 140) {
            const unsigned char *a = sPkt + sOff + 97;
            CfgStr(0, 33, ssid);
            sprintf(s, "ssid %s status %s ip %d.%d.%d.%d", ssid, st == 3 ? "connected" : "disconnected", a[0], a[1], a[2], a[3]);
        } else sprintf(s, "status %s (code %d)", st == 3 ? "connected" : "disconnected", st);
    } else if (strcmp(cmd, "CLOCK") == 0) {
        if ((err = Result(Call(DEV_CLOCK, 0x93, 1, 0, 0, NULL, 0, T_REPLY), s, "CLOCK")) != noErr) { *outlen = strlen(s); return err; }
        if (sPl < 6) { strcpy(s, "the FujiNet's clock answer was short"); *outlen = strlen(s); return 2; }
        {
            const unsigned char *c = sPkt + sOff;    /* day, month, year - 2000, hour, minute, second */
            sprintf(s, "OK %d %02d %02d %02d %02d %02d", 2000 + c[2], c[1], c[0], c[3], c[4], c[5]);
        }
    } else if (strncmp(cmd, "OPEN ", 5) == 0) {
        if ((err = OpenAsDrive(cmd + 5, s)) != noErr) { *outlen = strlen(s); return err; }
    } else if (strncmp(cmd, "READ ", 5) == 0) {
        return ReadAsDrive(cmd, out, outlen, echo);
    } else if (strcmp(cmd, "CLOSE") == 0) {
        if (sOpen) Call(DEV_N1, 0x43, 0, 0, 0, NULL, 0, T_REPLY);
        sOpen = false;
        strcpy(s, "OK");
    } else {
        sprintf(s, "%.40s is not available through a serial FujiNet", cmd);
        *outlen = strlen(s);
        return 2;
    }
    *outlen = strlen(s);
    return noErr;
}

static short SfStatus(long t)
{
    short rc = Call(DEV_N1, 0x53, 0, 0, 0, NULL, 0, t);
    if (rc != R_OK) return rc;
    if (sPl < 4) return R_BAD;
    sWait = sPkt[sOff] | ((long)sPkt[sOff + 1] << 8);
    sConn = sPkt[sOff + 2];
    sStat = sPkt[sOff + 3];
    return R_OK;
}

OSErr SfFetch(const char *url, char *buf, long max, long *got, long *total, int *code, char *why)
{
    unsigned char spec[256];
    long off = 0;
    unsigned long idleFrom = 0;
    short rc, n = (short)strlen(url);
    OSErr err = noErr;

    *got = 0; *total = 0; *code = 0; why[0] = 0; buf[0] = 0;
    if (!sUp) return fnOpnErr;
    if (n + 4 > 256) { strcpy(why, "the address is too long for a serial FujiNet (252 characters at most)"); return 2; }
    memcpy(spec, "N1:", 3); memcpy(spec + 3, url, n); spec[n + 3] = 0;
    if (sOpen) { Call(DEV_N1, 0x43, 0, 0, 0, NULL, 0, T_REPLY); sOpen = false; }
    rc = Call(DEV_N1, 0x4F, 2, 4, 0, spec, (short)(n + 4), T_OPEN);      /* mode 4: read (GET); no translation */
    if (rc != R_OK) return Result(rc, why, "the address");
    sOpen = true;
    rc = SfStatus(T_OPEN);                         /* the board does the GET when this STATUS arrives */
    if (rc != R_OK) { err = Result(rc, why, "STATUS"); goto done; }
    if (sStat == 207 || (sStat != 1 && sStat != 136 && sStat != 170)) {
        if (sStat == 207) strcpy(why, "no connection: the FujiNet could not reach the server");
        else sprintf(why, "the FujiNet said status %d", sStat);
        err = 2; goto done;
    }
    *code = sStat == 170 ? 404 : 200;
    while (off < max - 1) {
        short want;
        if (sWait <= 0) {
            if (sStat == 136) break;
            if ((rc = SfStatus(T_REPLY)) != R_OK) { err = Result(rc, why, "STATUS"); break; }
            if (sWait <= 0) {
                if (sStat != 1) break;               /* 136: the end; anything else: nothing more will come */
                if (!idleFrom) idleFrom = TickCount();
                else if (TickCount() - idleFrom > T_DRAIN) break;
                if (MbIdle(6)) { err = userCanceledErr; break; }
                continue;
            }
            idleFrom = 0;
        }
        want = sWait > 256 ? 256 : (short)sWait;
        if (off + want > max - 1) want = (short)(max - 1 - off);
        if ((rc = Call(DEV_N1, 0x52, 5, (unsigned short)want, 0, NULL, 0, T_REPLY)) != R_OK) { err = Result(rc, why, "READ"); break; }
        if (sPl < 1 || sPl > want) { strcpy(why, "the FujiNet sent the wrong number of bytes"); err = 2; break; }
        memcpy(buf + off, sPkt + sOff, sPl);
        off += sPl;
        sWait -= sPl;
    }
    *total = off + (sWait > 0 ? sWait : 0);
done:
    buf[off] = 0;
    *got = off;
    Call(DEV_N1, 0x43, 0, 0, 0, NULL, 0, T_REPLY);   /* NAK after a failed transfer still means closed */
    sOpen = false;
    return err;
}
