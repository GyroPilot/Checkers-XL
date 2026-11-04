/* xlproto.h: the ESPFuji mailbox page, version 2 (Macintosh XL), seen from the Mac.  Portable C: the same code is
 * built into ESPFuji XL (Retro68, 68000) and into the host test.  The layout is the drive's
 * (sw/ESProFile/mailbox_core.h): "LISAFUJI", version, sequence, kind, length, k, n, payload. */
#ifndef XLPROTO_H
#define XLPROTO_H

#define XL_PAGE      512
#define XL_PAYLOAD   492
#define XL_VERSION   2
#define XL_IDLE      0
#define XL_REQUEST   1
#define XL_REPLY     2

typedef struct {
    unsigned short version, seq, kind, len, k, n;
    unsigned long reserved;              /* bytes 16-19 as one number (a READ reply echoes its offset here) */
    const unsigned char *payload;
} XLHeader;

/* Fill a 512-byte page: header and payload, zero padded. */
void XLBuildPage(unsigned char *page, unsigned short seq, unsigned short kind, unsigned short k, unsigned short n,
                 const unsigned char *payload, unsigned short len);

/* 1 when the page is a version 2 mailbox page (fills h), else 0. */
int XLParsePage(const unsigned char *page, XLHeader *h);

/* 1 when the page is the reply to the request with this sequence number. */
int XLIsReplyTo(const unsigned char *page, unsigned short seq, XLHeader *h);

#endif
