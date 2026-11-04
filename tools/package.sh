#!/bin/bash
# package.sh: dist/Checkers XL <ver>.dsk (800K Disk Copy 4.2, for the XL's 800K drive or a Floppy Emu) and
# dist/Checkers XL <ver>.bin (MacBinary).  The application gets hasBundle (Retro68 leaves the Finder flags at 0).
# FujiNet XL's package.sh, with the names changed.
set -e
cd "$(dirname "$0")/.."
V=$(grep -o '"0\.[0-9a-z]*"' src/ckxl.r | head -1 | tr -d '"')
T=/Volumes/RetroDev/src/Retro68-build/toolchain/bin; W=test/work/pkg; mkdir -p $W dist
~/serialenv/bin/python - "$V" <<'PY'
import binascii, struct, sys
b = bytearray(open("build/CheckersXL.bin", "rb").read()); b[73] |= 0x20
b[124:126] = struct.pack(">H", binascii.crc_hqx(bytes(b[:124]), 0))
open("test/work/pkg/app.bin", "wb").write(b); open("dist/Checkers XL %s.bin" % sys.argv[1], "wb").write(b)
PY
sed -n '/^# Checkers XL/,/^## Build/p' README.md | sed '$d' | tr '\n' '\r' > $W/readme.txt
dd if=/dev/zero of=$W/disk.dsk bs=1024 count=800 2>/dev/null
( export HOME=$PWD/$W
  $T/hformat -l "Checkers XL $V" $W/disk.dsk >/dev/null
  $T/hmount $W/disk.dsk >/dev/null
  $T/hcopy -m $W/app.bin ":Checkers XL"
  $T/hcopy -r $W/readme.txt ":Read Me"
  $T/hattrib -t TEXT -c ttxt ":Read Me"
  $T/humount )
~/serialenv/bin/python - "$V" <<'PY'
import struct, sys
img = open("test/work/pkg/disk.dsk", "rb").read(); c = 0   # DC42: encoding 1 = 800K, format 0x22
for (w,) in struct.iter_unpack(">H", img): c = (c + w) & 0xFFFFFFFF; c = ((c >> 1) | (c << 31)) & 0xFFFFFFFF
n = ("Checkers XL " + sys.argv[1]).encode()
open("dist/Checkers XL %s.dsk" % sys.argv[1], "wb").write(bytes([len(n)]) + n + bytes(63 - len(n)) + struct.pack(">IIIIBBH", len(img), 0, c, 0, 1, 0x22, 0x0100) + img)
PY
ls -l dist
