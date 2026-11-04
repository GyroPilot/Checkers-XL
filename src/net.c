/* net.c: see net.h.  LisaCheckers' CKR-NET.TEXT, with the mailbox (mailbox.c) as the only route. */
#include <Multiverse.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "rules.h"
#include "mailbox.h"
#include "net.h"

#define kReply 2048                         /* a state is about 300 bytes; the legal line can be long */

CnState  cn;
CnTables cnTb;
char     cnTable[12] = "c1", cnName[12] = "macxl";
static char sReply[kReply];

void CnCleanName(const char *in, char *out)
{
    short n = 0;
    for (; *in && n < 8; in++) {
        char c = *in;
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out[n++] = c;
    }
    out[n] = 0;
}

static short Side(const char *s)
{
    if (!strncmp(s, "black", 5)) return CK_BLACK;
    if (!strncmp(s, "white", 5)) return CK_WHITE;
    return 0;
}

static void Copy(char *to, const char *from, short max)   /* to the end of the line */
{
    short n = 0;
    while (from[n] && from[n] != '\n' && from[n] != '\r' && n < max - 1) { to[n] = from[n]; n++; }
    to[n] = 0;
}

static void StateLine(char *l)
{
    char *v = strchr(l, ' ');
    if (v) *v++ = 0; else v = l + strlen(l);
    if (!strcmp(l, "ERROR")) { Copy(cn.err, v, sizeof cn.err); return; }
    if (!strcmp(l, "CK1")) { cn.got = true; return; }
    if (!strcmp(l, "status")) cn.stat = !strcmp(v, "play") ? cnPlay : !strcmp(v, "over") ? cnOver : cnLobby;
    else if (!strcmp(l, "you")) cn.you = Side(v);
    else if (!strcmp(l, "turn")) cn.turn = Side(v);
    else if (!strcmp(l, "draw")) cn.draw = Side(v);
    else if (!strcmp(l, "black")) Copy(cn.black, v, sizeof cn.black);
    else if (!strcmp(l, "white")) Copy(cn.white, v, sizeof cn.white);
    else if (!strcmp(l, "last")) Copy(cn.last, v, sizeof cn.last);
    else if (!strcmp(l, "result")) Copy(cn.result, v, sizeof cn.result);
    else if (!strcmp(l, "prompt")) Copy(cn.prompt, v, sizeof cn.prompt);
    else if (!strcmp(l, "moves")) cn.moves = (short)atoi(v);
    else if (!strcmp(l, "board") && strlen(v) >= 32) {
        short i;
        for (i = 0; i < 32; i++) {
            char c = v[i];
            cn.board[i + 1] = (signed char)(c == 'b' ? 1 : c == 'B' ? 2 : c == 'w' ? -1 : c == 'W' ? -2 : 0);
        }
        cn.haveBd = true;
    }
    /* legal: not wanted, the rules know the moves (as on the Lisa) */
}

static void TableLine(char *l)              /* T|<id>|<name>|<n>/2|<status> */
{
    char *f[5];
    short k = 0;
    if (cnTb.n >= CN_TBMAX) return;
    f[k++] = l;
    while (k < 5 && (l = strchr(l, '|')) != NULL) { *l++ = 0; f[k++] = l; }
    if (k < 5 || strcmp(f[0], "T")) return;
    Copy(cnTb.id[cnTb.n], f[1], sizeof cnTb.id[0]);
    Copy(cnTb.name[cnTb.n], f[2], sizeof cnTb.name[0]);
    Copy(cnTb.seats[cnTb.n], f[3], sizeof cnTb.seats[0]);
    Copy(cnTb.st[cnTb.n], f[4], sizeof cnTb.st[0]);
    cnTb.n++;
}

OSErr CnGet(const char *path, char *why)
{
    char url[200], dw[200];
    long got, total;
    int code;
    OSErr err;
    char *p, *e;
    Boolean tables = !strcmp(path, "/tables");

    why[0] = 0;
    sprintf(url, "%s%s?table=%s&player=%s%s", CN_SERVER, path, cnTable, cnName, tables ? "&dev=1" : "");
    err = MbFetch(url, sReply, kReply, &got, &total, &code, dw);
    if (err) { MbErrorText("Checkers server", err, dw, why); return err; }
    if (tables) cnTb.n = 0;
    else { cn.got = false; cn.err[0] = 0; }
    for (p = sReply; *p; p = e) {
        e = p;
        while (*e && *e != '\n' && *e != '\r') e++;
        if (*e) *e++ = 0;
        if (tables) TableLine(p); else StateLine(p);
    }
    if (tables) {
        if (cnTb.n == 0) { sprintf(why, "The server listed no tables (HTTP %d).", code); return 3; }
    } else if (!cn.got && strcmp(path, "/leave")) {
        if (cn.err[0]) sprintf(why, "Server: %s", cn.err);
        else sprintf(why, "The server sent something unexpected (HTTP %d).", code);
        return 3;
    }
    return noErr;
}

void CnLastMove(CkMove *mv)
{
    const char *s = cn.last;
    short n = 0;
    mv->nHop = 0; mv->nCap = 0;
    if (*s < '0' || *s > '9') return;
    for (;;) {
        short v = 0;
        while (*s >= '0' && *s <= '9') v = (short)(v * 10 + (*s++ - '0'));
        if (v < 1 || v > 32 || n > CK_MAXHOP) { mv->nHop = 0; return; }
        mv->hop[n++] = (unsigned char)v;
        if (*s == 'x') mv->nCap = 1;
        if (*s != '-' && *s != 'x') break;
        s++;
    }
    mv->nHop = (unsigned char)(n - 1);
}
