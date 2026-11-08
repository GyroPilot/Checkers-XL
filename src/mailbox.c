/* mailbox.c: see mailbox.h. */
#include <Multiverse.h>
#include <string.h>
#include <stdio.h>
#include "xlproto.h"
#include "mailbox.h"
#include "serfuji.h"

/* AllocContig: Retro68 has no glue for it, so the trap itself (_Allocate with the contiguous bit, $A210). */
#pragma parameter __D0 PBAllocContigTrap(__A0)
pascal OSErr PBAllocContigTrap(ParmBlkPtr pb) M68K_INLINE(0xA210);

#ifndef noCacheMask
#define noCacheMask 0x20                    /* ioPosMode: past the File Manager's cache */
#endif

static short sRef = 0;
static unsigned short sSeq = 0, sNextK = 0;


static OSErr PageIO(Boolean write, short k, unsigned char *page)
{
    ParamBlockRec pb;
    memset(&pb, 0, sizeof pb);
    pb.ioParam.ioRefNum = sRef;
    pb.ioParam.ioBuffer = (Ptr)page;
    pb.ioParam.ioReqCount = XL_PAGE;
    pb.ioParam.ioPosMode = fsFromStart | noCacheMask;
    pb.ioParam.ioPosOffset = (long)k * XL_PAGE;
    return write ? PBWriteSync(&pb) : PBReadSync(&pb);
}

/* HFS: is the file one extent?  The FCB's first extent record is at offset 38; the second count is 0 when
 * everything is in the first.  MFS keeps no extents: trusted. */
static Boolean Contiguous(short ref)
{
    Ptr fcbs = *(Ptr volatile *)0x034E;     /* FCBSPtr */
    Ptr fcb = fcbs + ref;
    Ptr vcb = *(Ptr *)(fcb + 20);
    if (*(unsigned short *)(vcb + 8) != 0x4244) return true;
    return *(unsigned short *)(fcb + 38 + 6) == 0;
}

/* Every page an idle page, so the drive's checks know the file (1.1b2 answers only on stored mailbox pages). */
static OSErr Fill(short vref)
{
    static unsigned char page[XL_PAGE];
    short k;
    OSErr err;
    for (k = 0; k < MB_PAGES; k++) {
        XLBuildPage(page, 0, XL_IDLE, (unsigned short)k, MB_PAGES, NULL, 0);
        if ((err = PageIO(true, k, page)) != noErr) return err;
    }
    FlushVol(NULL, vref);
    return noErr;
}

static OSErr OpenExisting(ConstStr255Param name, short vref)
{
    long eof = 0;
    OSErr err = FSOpen(name, vref, &sRef);
    if (err) { sRef = 0; return err; }
    GetEOF(sRef, &eof);
    if (eof < (long)MB_PAGES * XL_PAGE || !Contiguous(sRef)) { FSClose(sRef); sRef = 0; return dskFulErr; }
    if ((err = Fill(vref)) != noErr) { FSClose(sRef); sRef = 0; }
    return err;
}

static OSErr MbxOpen(char *where)
{
    unsigned char named[] = "\pESPFuji Mailbox", local[] = "\pMAILBOX.DATA";
    HParamBlockRec vp;
    Str255 vname;
    short i, vref;
    OSErr err;

    for (i = 1; ; i++) {                    /* 1. "ESPFuji Mailbox" at the top of any volume */
        memset(&vp, 0, sizeof vp);
        vp.volumeParam.ioNamePtr = vname;
        vp.volumeParam.ioVolIndex = i;
        if (PBHGetVInfoSync(&vp) != noErr) break;
        if (OpenExisting(named, vp.volumeParam.ioVRefNum) == noErr) {
            vname[vname[0] + 1] = 0;
            sprintf(where, "Mailbox: \"ESPFuji Mailbox\" on %s.", (char *)vname + 1);
            return noErr;
        }
    }
    GetVol(NULL, &vref);                    /* 2. MAILBOX.DATA beside this application */
    if (OpenExisting(local, vref) == noErr) { strcpy(where, "Mailbox: MAILBOX.DATA beside this application."); return noErr; }
    err = Create(local, vref, 'EFXL', 'MBOX');
    if (err && err != dupFNErr) return err;
    if ((err = FSOpen(local, vref, &sRef)) != noErr) { sRef = 0; return err; }
    SetEOF(sRef, 0);
    {
        ParamBlockRec pb;
        memset(&pb, 0, sizeof pb);
        pb.ioParam.ioRefNum = sRef;
        pb.ioParam.ioReqCount = (long)MB_PAGES * XL_PAGE;
        err = PBAllocContigTrap(&pb);
    }
    if (err == noErr) err = SetEOF(sRef, (long)MB_PAGES * XL_PAGE);
    if (err == noErr && !Contiguous(sRef)) err = dskFulErr;
    if (err == noErr) err = Fill(vref);
    if (err) { FSClose(sRef); sRef = 0; return err; }
    strcpy(where, "Mailbox: MAILBOX.DATA made beside this application.");
    return noErr;
}


static long sMbWait = MB_WAIT;           /* ticks a mailbox reply may take (3 s for the HELLO probe at launch) */

static OSErr MbxTransact(const char *cmd, unsigned char *out, short *outlen, unsigned long *echo)
{
    static unsigned char page[XL_PAGE];
    XLHeader h;
    unsigned long deadline;
    short k, j = 1, tries = 0;
    OSErr err;

    if (!sRef) return fnOpnErr;
    if (++sSeq == 0) sSeq = 1;
    k = (short)sNextK;
    sNextK = (unsigned short)((sNextK + 1) % MB_PAGES);
    XLBuildPage(page, sSeq, XL_REQUEST, (unsigned short)k, MB_PAGES, (const unsigned char *)cmd, (unsigned short)strlen(cmd));
    if ((err = PageIO(true, k, page)) != noErr) return err;
    deadline = TickCount() + sMbWait;
    while (TickCount() < deadline) {
        short p = (short)((k + j) % MB_PAGES);   /* the other pages in turn: a cached page comes back stale */
        j = (short)(j % (MB_PAGES - 1) + 1);
        if (PageIO(false, p, page) == noErr && XLIsReplyTo(page, sSeq, &h)) {
            memcpy(out, h.payload, h.len);
            *outlen = (short)h.len;
            if (echo) *echo = h.reserved;
            return noErr;
        }
        if (MbCancelPressed()) return userCanceledErr;
        MbBreathe();
        /* 3 Nov 2026: after the first ten reads (a READ's reply is back by then), pause six ticks between reads.
         * Reading back to back left the keyboard almost no time to be heard (the disk driver keeps interrupts off
         * while it transfers): Command-period did nothing on Tim's XL, nor in QEMU.  It also spares the drive. */
        if (++tries > 10 && MbIdle(6)) return userCanceledErr;
    }
    return 1;
}

static OSErr MbxFetch(const char *url, char *buf, long max, long *got, long *total, int *code, char *why)
{
    unsigned char out[XL_PAYLOAD + 1];
    char cmd[XL_PAYLOAD + 1];
    short n;
    unsigned long echo, t = 0, off = 0;
    OSErr err;

    *got = 0; *total = 0; *code = 0; why[0] = 0; buf[0] = 0;
    if (strlen(url) + 5 > XL_PAYLOAD) { strcpy(why, "the address is too long (487 characters at most)"); return 2; }
    sprintf(cmd, "OPEN %s", url);
    if ((err = MbxTransact(cmd, out, &n, NULL)) != noErr) return err;
    out[n] = 0;
    if (sscanf((const char *)out, "OK %lu %d", &t, code) != 2) {     /* the drive's own words, as the Lisa tools now show */
        if (n > 120) n = 120;
        memcpy(why, out, n); why[n] = 0;
        return 2;
    }
    *total = (long)t;
    while (off < t && (long)off < max - 1) {
        short want = XL_PAYLOAD;
        if ((long)off + want > max - 1) want = (short)(max - 1 - (long)off);
        sprintf(cmd, "READ %lu %d", off, want);
        if ((err = MbxTransact(cmd, out, &n, &echo)) != noErr) break;
        /* 1.1b1 echoes the block number for offset 0; the sequence number proves the reply fresh. */
        if (n <= 0 || (off != 0 && echo != off)) { strcpy(why, "the drive answered out of step"); err = 2; break; }
        memcpy(buf + off, out, n);
        off += (unsigned long)n;
    }
    buf[off] = 0;
    *got = (long)off;
    MbxTransact("CLOSE", out, &n, NULL);
    return err;
}

/* ---- The route: the ESPFuji mailbox or a serial FujiNet (6 Nov 2026) ----
 * One choice for every Mac XL application, kept in "XL FujiNet Settings" in the System Folder: Automatic (the
 * ESProFile when its drive answers HELLO, else a FujiNet on Serial A, then Serial B), or one route. */
#define VIA_MB   0
#define VIA_SER  1
static short sVia = VIA_MB, sRoute = MB_AUTO, sMenuBase = -1;
static long sBaud = 19200;
static MenuHandle sMenu = NULL;
static const unsigned char kPrefName[] = "\pXL FujiNet Settings";

static short PrefVol(void)
{
    SysEnvRec env;
    if (SysEnvirons(1, &env) == noErr && env.sysVRefNum) return env.sysVRefNum;
    return 0;
}

static void LoadRoute(void)
{
    short ref;
    long n = 8;
    unsigned char b[8];
    if (FSOpen(kPrefName, PrefVol(), &ref) != noErr) return;
    if (FSRead(ref, &n, b) == noErr && n == 8 && b[0] == 'R' && b[1] <= MB_PRINTER) {
        sRoute = b[1];
        sBaud = ((long)b[4] << 24) | ((long)b[5] << 16) | ((long)b[6] << 8) | b[7];
        if (sBaud != 9600 && sBaud != 19200) sBaud = 19200;
    }
    FSClose(ref);
}

static void SaveRoute(void)
{
    short ref, v = PrefVol();
    long n = 8;
    unsigned char b[8] = { 'R', 0, 0, 0, 0, 0, 0, 0 };
    b[1] = (unsigned char)sRoute;
    b[4] = (unsigned char)(sBaud >> 24); b[5] = (unsigned char)(sBaud >> 16); b[6] = (unsigned char)(sBaud >> 8); b[7] = (unsigned char)sBaud;
    Create(kPrefName, v, 'EFXL', 'pref');
    if (FSOpen(kPrefName, v, &ref) != noErr) return;
    SetFPos(ref, fsFromStart, 0);
    FSWrite(ref, &n, b);
    SetEOF(ref, 8);
    FSClose(ref);
    FlushVol(NULL, v);
}

static void LoadOnce(void) { static Boolean done = false; if (!done) { LoadRoute(); done = true; } }

Boolean MbReady(void) { return sVia == VIA_SER ? SfUp() : sRef != 0; }

void MbClose(void)
{
    if (sRef) FSClose(sRef);
    sRef = 0;
    SfClose();
}

static Boolean DriveAnswers(void)                /* a short HELLO: is an ESPFuji drive behind the mailbox? */
{
    unsigned char out[XL_PAYLOAD + 1];
    short n;
    OSErr err;
    sMbWait = 3 * 60;
    err = MbxTransact("HELLO", out, &n, NULL);
    sMbWait = MB_WAIT;
    return err == noErr;
}

OSErr MbOpen(char *where)
{
    char line[400];
    OSErr err;
    where[0] = 0;                                /* 1.1: callers print where even on errors */
    LoadOnce();
    MbClose();
    sVia = VIA_MB;
    if (sRoute == MB_ESPROFILE) return MbxOpen(where);
    if (sRoute == MB_MODEM || sRoute == MB_PRINTER) {
        sVia = VIA_SER;
        if (SfOpen(sRoute == MB_PRINTER ? SF_PRINTER : SF_MODEM, &sBaud, where) == noErr) SaveRoute();
        return noErr;                            /* where says what happened; requests say "no FujiNet" until it answers */
    }
    SF_TRACE("  route: Automatic, the ESProFile mailbox first");
    err = MbxOpen(where);                        /* Automatic */
    SF_TRACE(err == noErr ? "  route: mailbox open, HELLO to the drive (3 s)" : "  route: no mailbox");
    if (err == noErr && DriveAnswers()) return noErr;
    if (SfOpen(SF_MODEM, &sBaud, line) == noErr || SfOpen(SF_PRINTER, &sBaud, line) == noErr) {
        if (sRef) FSClose(sRef);
        sRef = 0;
        sVia = VIA_SER;
        strcpy(where, line);
        SaveRoute();
        return noErr;
    }
    if (err == noErr) strcat(where, "  (No answer from the drive yet, and no serial FujiNet on Serial A or B.)");
    if (strstr(line, "AppleTalk")) strcpy(where, line);   /* the AppleTalk 57 message matters more */
    return err;
}

void MbRouteText(char *out)
{
    if (sVia == VIA_SER) { if (SfUp()) SfRouteText(out); else strcpy(out, "a serial FujiNet (not answering)"); }
    else strcpy(out, sRef ? "the ESProFile (ESPFuji mailbox)" : "no route");
}

static void CheckRoute(void)
{
    short i;
    if (!sMenu || sMenuBase < 0) return;
    for (i = 0; i <= MB_PRINTER; i++) CheckItem(sMenu, sMenuBase + 2 + i, i == sRoute);
}

void MbRouteMenu(MenuHandle m)
{
    sMenu = m;
    sMenuBase = CountMItems(m);
    AppendMenu(m, "\p(-");
    AppendMenu(m, "\pAutomatic Route");
    AppendMenu(m, "\pUse the ESProFile");
    AppendMenu(m, "\pUse Serial A");
    AppendMenu(m, "\pUse Serial B");
    AppendMenu(m, "\pMake a New Mailbox");        /* 1.1 (Tim, 7 Nov): clear a damaged MAILBOX.DATA by hand */
    LoadOnce();
    CheckRoute();
}

/* 1.1 (7 Nov 2026): Make a New Mailbox.  Deletes MAILBOX.DATA beside this application (a half-made or damaged one:
 * Salvo XL got error -127 on the 2/10) and opens the route again, which makes a fresh one.  An "ESPFuji Mailbox" at the
 * top of a volume is left alone. */
static void NewMailbox(char *where)
{
    unsigned char local[] = "\pMAILBOX.DATA";
    short vref;
    OSErr err, e2;
    char line[400];
    MbClose();
    GetVol(NULL, &vref);
    err = FSDelete(local, vref);
    if (err != noErr && err != fnfErr) {
        sprintf(where, "Make a New Mailbox: MAILBOX.DATA could not be deleted (error %d).  Delete it in the Finder.", (int)err);
        return;
    }
    e2 = MbOpen(line);
    /* 1.1: over serial the mailbox is not used, so say so instead of naming the serial route as if it were the drive */
    if (e2 == noErr && sVia == VIA_SER) sprintf(where, "New mailbox made beside the application; only the ESProFile route uses it.  %s", line);
    else if (e2 == noErr) sprintf(where, "New mailbox.  %s  (If the drive does not answer now, restart.)", line);
    else sprintf(where, "Make a New Mailbox: %s (error %d).", line[0] ? line : "no mailbox made", (int)e2);
}

Boolean MbRouteHit(MenuHandle m, short item, char *where)
{
    OSErr err;
    if (m != sMenu || sMenuBase < 0) return false;
    if (item == sMenuBase + 3 + MB_PRINTER) { NewMailbox(where); return true; }
    if (item < sMenuBase + 2 || item > sMenuBase + 2 + MB_PRINTER) return false;
    sRoute = item - sMenuBase - 2;
    SaveRoute();
    CheckRoute();
    where[0] = 0;
    err = MbOpen(where);
    if (err != noErr && !where[0])
        sprintf(where, "No route (error %d): no mailbox and no serial FujiNet.  Make a New Mailbox (same menu) starts the mailbox again.", (int)err);
    return true;
}

OSErr MbTransact(const char *cmd, unsigned char *out, short *outlen, unsigned long *echo)
{
    if (sVia == VIA_SER) return SfTransact(cmd, out, outlen, echo);
    return MbxTransact(cmd, out, outlen, echo);
}

OSErr MbFetch(const char *url, char *buf, long max, long *got, long *total, int *code, char *why)
{
    if (sVia == VIA_SER) return SfFetch(url, buf, max, got, total, code, why);
    return MbxFetch(url, buf, max, got, total, code, why);
}

void MbErrorText(const char *what, OSErr err, const char *why, char *out)
{
    if (sVia == VIA_SER) {
        char r[80];
        MbRouteText(r);
        if (err == 1) { sprintf(out, "%s: no answer from %s in 5 s. Check the cable, and that the FujiNet has firmware v1.6.1 or later.", what, r); return; }
        if (err == fnOpnErr) { sprintf(out, "%s: no serial FujiNet answered. Check the cable and the port, then choose it again in the Network menu.", what); return; }
    }

    if (err == 1) sprintf(out, "%s: no answer in 45 s. Is the mailbox on the ESPFuji drive (firmware 1.1 or later)?", what);
    else if (err == userCanceledErr) sprintf(out, "%s: stopped.", what);
    else if (err == 2) sprintf(out, "%s: %s", what, why);
    else if (err == fnOpnErr) sprintf(out, "%s: no mailbox. Copy this application to the ESProFile (ESPFuji) volume and open it there; it cannot reach the drive from a server or floppy.", what);   /* -38: no mailbox open (4 Nov 2026) */
    else sprintf(out, "%s: error %d.", what, (int)err);
}
