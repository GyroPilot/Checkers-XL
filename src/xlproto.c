/* xlproto.c: see xlproto.h. */
#include <string.h>
#include "xlproto.h"

static const unsigned char kSig[8] = { 'L', 'I', 'S', 'A', 'F', 'U', 'J', 'I' };

static void put16(unsigned char *p, unsigned short v) { p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v; }
static unsigned short get16(const unsigned char *p) { return (unsigned short)((p[0] << 8) | p[1]); }

void XLBuildPage(unsigned char *page, unsigned short seq, unsigned short kind, unsigned short k, unsigned short n,
                 const unsigned char *payload, unsigned short len)
{
    if (len > XL_PAYLOAD) len = XL_PAYLOAD;
    memset(page, 0, XL_PAGE);
    memcpy(page, kSig, 8);
    put16(page + 8, XL_VERSION);
    put16(page + 10, seq);
    put16(page + 12, kind);
    put16(page + 14, len);
    put16(page + 16, k);
    put16(page + 18, n);
    if (len) memcpy(page + 20, payload, len);
}

int XLParsePage(const unsigned char *page, XLHeader *h)
{
    if (memcmp(page, kSig, 8) != 0) return 0;
    h->version = get16(page + 8);
    if (h->version != XL_VERSION) return 0;
    h->seq = get16(page + 10);
    h->kind = get16(page + 12);
    h->len = get16(page + 14);
    h->k = get16(page + 16);
    h->n = get16(page + 18);
    h->reserved = ((unsigned long)h->k << 16) | h->n;
    h->payload = page + 20;
    return h->len <= XL_PAYLOAD;
}

int XLIsReplyTo(const unsigned char *page, unsigned short seq, XLHeader *h)
{
    return XLParsePage(page, h) && h->kind == XL_REPLY && h->seq == seq;
}
