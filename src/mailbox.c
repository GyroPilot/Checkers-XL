/* mailbox.c: see mailbox.h. */
#include <Multiverse.h>
#include <string.h>
#include <stdio.h>
#include "xlproto.h"
#include "mailbox.h"

/* AllocContig: Retro68 has no glue for it, so the trap itself (_Allocate with the contiguous bit, $A210). */
#pragma parameter __D0 PBAllocContigTrap(__A0)
pascal OSErr PBAllocContigTrap(ParmBlkPtr pb) M68K_INLINE(0xA210);

#ifndef noCacheMask
#define noCacheMask 0x20                    /* ioPosMode: past the File Manager's cache */
#endif

static short sRef = 0;
static unsigned short sSeq = 0, sNextK = 0;

Boolean MbReady(void) { return sRef != 0; }

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

OSErr MbOpen(char *where)
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

void MbClose(void) { if (sRef) FSClose(sRef); sRef = 0; }

OSErr MbTransact(const char *cmd, unsigned char *out, short *outlen, unsigned long *echo)
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
    deadline = TickCount() + MB_WAIT;
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

OSErr MbFetch(const char *url, char *buf, long max, long *got, long *total, int *code, char *why)
{
    unsigned char out[XL_PAYLOAD + 1];
    char cmd[XL_PAYLOAD + 1];
    short n;
    unsigned long echo, t = 0, off = 0;
    OSErr err;

    *got = 0; *total = 0; *code = 0; why[0] = 0; buf[0] = 0;
    if (strlen(url) + 5 > XL_PAYLOAD) { strcpy(why, "the address is too long (487 characters at most)"); return 2; }
    sprintf(cmd, "OPEN %s", url);
    if ((err = MbTransact(cmd, out, &n, NULL)) != noErr) return err;
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
        if ((err = MbTransact(cmd, out, &n, &echo)) != noErr) break;
        /* 1.1b1 echoes the block number for offset 0; the sequence number proves the reply fresh. */
        if (n <= 0 || (off != 0 && echo != off)) { strcpy(why, "the drive answered out of step"); err = 2; break; }
        memcpy(buf + off, out, n);
        off += (unsigned long)n;
    }
    buf[off] = 0;
    *got = (long)off;
    MbTransact("CLOSE", out, &n, NULL);
    return err;
}

void MbErrorText(const char *what, OSErr err, const char *why, char *out)
{
    if (err == 1) sprintf(out, "%s: no answer in 45 s. Is the mailbox on the ESPFuji drive (firmware 1.1 or later)?", what);
    else if (err == userCanceledErr) sprintf(out, "%s: stopped.", what);
    else if (err == 2) sprintf(out, "%s: %s", what, why);
    else sprintf(out, "%s: error %d.", what, (int)err);
}
