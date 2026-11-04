#!/usr/bin/env python3
"""fujisim.py: an ESPFuji drive (firmware 1.1b2, mailbox protocol version 2) for a Mac in QEMU, so the Mac XL apps can
be tested against the real web services without the hardware.

    ~/serialenv/bin/python tools/fujisim.py test/work/fuji.img [--log test/out/fujisim.log]

QEMU's second SCSI disk is a small HFS image (test/run_qemu.sh) holding the app's mailbox file.  The Mac writes its
pages through the File Manager with the cache bypassed, so they land in the image file at once.  This program watches
the image for version 2 pages, answers each new request the way ESProFile_FujiNet.ino's mbServe does (HELLO, WIFI,
CLOCK, OPEN, READ, CLOSE, ECHO), and writes the reply page over every other page of the mailbox, which is what the
drive's read hook does on the real thing.  It only ever writes inside the mailbox's own pages.
Echo (bytes 16-19): a READ's offset, else the page's k/n, as 1.1b2.  OPEN: GET, redirects followed, 32 KB at most."""
import sys, os, time, struct, ssl, urllib.request, datetime, argparse
SIG = b"LISAFUJI"
ap = argparse.ArgumentParser(); ap.add_argument("image"); ap.add_argument("--log"); ap.add_argument("--utc-offset", type=float, default=None); ap.add_argument("--slow", type=float, default=0, help="seconds an OPEN waits first (to test Command-period)")
ap.add_argument("--rewrite", action="append", default=[], metavar="FROM=TO", help="send URLs starting FROM to TO instead (a local test server)")
a = ap.parse_args()
log = open(a.log, "a") if a.log else sys.stdout
def say(*x): print(time.strftime("%T"), *x, file=log, flush=True)
body, body_open, started, nreq = b"", False, time.time(), 0
ctx = ssl.create_default_context(); ctx.check_hostname = False; ctx.verify_mode = ssl.CERT_NONE   # as the drive: TLS unchecked
def serve(cmd):
    global body, body_open, nreq
    nreq += 1
    if cmd.startswith("ECHO "): return "OK " + cmd[5:], None
    if cmd == "HELLO": return "ESPFuji 1.1b2 (fujisim) up %ds connected 10.0.2.15 req %d heap 99k/99k" % (time.time() - started, nreq), None
    if cmd == "WIFI": return "ssid fujisim status connected ip 10.0.2.15 rssi -40 reason 0", None
    if cmd == "CLOCK":
        t = datetime.datetime.now() if a.utc_offset is None else datetime.datetime.utcnow() + datetime.timedelta(hours=a.utc_offset)
        return t.strftime("OK %Y %m %d %H %M %S"), None
    if cmd.startswith("OPEN "):
        if a.slow: time.sleep(a.slow)
        url = cmd[5:].strip()
        if url.upper().startswith("N1:"): url = url[3:]
        for rw in a.rewrite:
            f, t = rw.split("=", 1)
            if url.startswith(f): say("rewrite", url, "->", t + url[len(f):]); url = t + url[len(f):]; break
        try:
            rq = urllib.request.Request(url, headers={"User-Agent": "ESPFuji 1.1b2 (fujisim)"})
            with urllib.request.urlopen(rq, timeout=25, context=ctx) as r:
                data = r.read(32769); code = r.status
        except urllib.error.HTTPError as e:
            data, code = e.read(32769), e.code
        except Exception as e:
            body_open = False; return "ERROR " + str(e)[:200], None
        trunc = len(data) > 32768; body = data[:32768]; body_open = True
        return "OK %d %d%s" % (len(body), code, " truncated" if trunc else ""), None
    if cmd.startswith("READ "):
        try: off, cnt = map(int, cmd[5:].split())
        except ValueError: return "ERROR READ wants offset and count", None
        if not body_open: return "ERROR nothing open", None
        cnt = min(cnt, 492); off = min(off, len(body)); cnt = min(cnt, len(body) - off)
        return body[off:off + cnt], off
    if cmd == "CLOSE": body_open = False; body = b""; return "OK closed", None
    return "ERROR unknown request", None
def page(seq, kind, k, n, payload, echo=None):
    payload = payload.encode("latin-1", "replace") if isinstance(payload, str) else payload
    res = echo if echo is not None else (k << 16) | n
    p = SIG + struct.pack(">HHHHI", 2, seq, kind, len(payload), res) + payload
    return p + bytes(512 - len(p))
say("fujisim on", a.image)
pages, last_seq, last_scan = {}, None, 0
while True:
    try:
        with open(a.image, "r+b") as f:
            lost = any((f.seek(o), f.read(10))[1] != SIG + b"\x00\x02" for o in pages.values())
            if not pages or lost and time.time() - last_scan > 2:   # map the mailbox: only idle and request pages
                img = f.read() if not f.seek(0) else b""; last_scan = time.time(); found = {}
                for i in range(0, len(img) - 511, 512):            # say where they are (replies carry echoes in 16-19)
                    if img[i:i + 8] == SIG and img[i + 8:i + 10] == b"\x00\x02" and img[i + 12:i + 14] in (b"\x00\x00", b"\x00\x01"):
                        k, n = struct.unpack(">HH", img[i + 16:i + 20]); found.setdefault(k, i)
                if len(found) >= 2 and found != pages: pages = found; say("mailbox: %d pages" % len(pages))
            for k, off in pages.items():
                f.seek(off); p = f.read(512)
                if p[:8] != SIG or p[8:10] != b"\x00\x02": continue
                seq, kind, ln, kk, n = struct.unpack(">HHHHH", p[10:20])
                if kind != 1 or seq == last_seq: continue
                last_seq = seq
                cmd = p[20:20 + ln].decode("latin-1")
                out, echo = serve(cmd)
                say("seq %d k %d: %s -> %s" % (seq, k, cmd[:90], (out[:70] if isinstance(out, str) else "%d bytes" % len(out))))
                rep = page(seq, 2, kk, n, out, echo)
                for k2, off2 in pages.items():
                    if k2 != k: f.seek(off2); f.write(rep)
                f.flush(); os.fsync(f.fileno())
                break
    except FileNotFoundError:
        pass
    time.sleep(0.05)
