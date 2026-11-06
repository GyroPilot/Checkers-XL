#!/bin/bash
# run_qemu.sh: Checkers XL on System 7.6.1 in the QEMU Quadra 800 (FujiNet XL's harness, monitor port 45822),
# with tools/fujisim.py as the ESPFuji drive.
#   bash test/run_qemu.sh [seconds]            SCEN=<name>: the steps in test/scen_<name>.txt (default: local)
# "ESPFuji Mailbox" (64 idle pages) goes at the top of the boot disk and the app in Startup Items (NOMB=1: no
# mailbox); fujisim watches the disk image for the app's requests.  REWRITE=1 sends the checkers server's address
# to a local copy of it (http://127.0.0.1:8090: start server/ckr_server.py 8090 first; test/net_test.sh does).
# A step line is "<t> <what> <args>", t a multiple of 5 s (the loop steps in 5 s):
#   key <qemu key>       e.g. key ret, key meta_l-n, key shift-1
#   type <text>          letters and digits
#   click <x> <y>        a mouse click at screen point x,y (tools/qclick.py finds the pointer in screendumps)
#   sq <n> [b|w]         a click on checkers square n (you black or white)
#   drag <x1> <y1> <x2> <y2>  press, move, release (a menu item)
#   mon <command>        anything else for the QEMU monitor
#   sh <command>         a shell command (test/net_test.sh: stop the local server)
# ICOUNT=10: one instruction = 1 us of Mac time (QEMU allows no more; a 5 MHz 68000 is about half that speed), to see how long the Mac thinks on an XL.
# A screenshot every 5 s from 60 s on (the image's PRAM sets 800 x 600; the board and panel are laid out for 512 x 342).
set -u
SERARGS=(); if [ -n "${SER:-}" ]; then SIM=/Volumes/RetroDev/src/moof-setup/pcfuji/pcfuji_sim.py; SERARGS=(-serial tcp:127.0.0.1:45900); fi   # SER=1: PCFuji on the modem port, no drive answers
cd "$(dirname "$0")/.."
T=/Volumes/RetroDev/src/Retro68-build/toolchain/bin
Q=/Volumes/RetroDev/emulators/qemu
SECS=${1:-200}
SCEN=${SCEN:-local}
W=test/work; OUT=test/out/$SCEN; PORT=45822
mkdir -p $W $OUT
rm -f "test/out/$SCEN"/screen_*.png "test/out/$SCEN"/fujisim.log "test/out/$SCEN"/clicks.log
cp -c $Q/hd.img $W/q.img
cp $Q/pram.img $W/q_pram.img
~/serialenv/bin/python -c "
import struct
p=lambda k: (b'LISAFUJI'+struct.pack('>HHHHHH',2,0,0,0,k,64)).ljust(512,b'\0')
open('$W/mailbox.bin','wb').write(b''.join(p(k) for k in range(64)))"
SI=":System Folder:Startup Items"
( export HOME=$PWD/$W
  $T/hmount $W/q.img >/dev/null
  $T/hcopy -m ${APP:-build/CheckersXL.bin} "$SI:Checkers XL"
  if [ -z "${NOMB:-}" ]; then
    $T/hcopy -r $W/mailbox.bin ":ESPFuji Mailbox"
    $T/hattrib -t MBOX -c EFXL ":ESPFuji Mailbox"
  fi
  $T/humount )
sim=
if [ -z "${NOSIM:-}" ]; then
  ~/serialenv/bin/python ${SIM:-tools/fujisim.py} $W/q.img --log $OUT/fujisim.log --utc-offset 0 ${SLOW:+--slow $SLOW} \
      ${REWRITE:+--rewrite https://lisa-gw1.tailc2f141.ts.net/checkers=http://127.0.0.1:8090} &
  sim=$!; [ -n "${SER:-}" ] && sleep 2
fi
/opt/local/bin/qemu-system-m68k -M q800 -m 128 -bios $Q/Q800.ROM -display none -g ${G:-1152x870x8} ${ICOUNT:+-icount shift=$ICOUNT,sleep=off} \
    -drive file=$W/q_pram.img,format=raw,if=mtd \
    -device scsi-hd,scsi-id=0,drive=hd0 \
    -drive file=$W/q.img,format=raw,media=disk,if=none,id=hd0 \
    -nic user,model=dp83932 ${SERARGS[@]+"${SERARGS[@]}"} \
    -monitor tcp:127.0.0.1:$PORT,server,nowait > $W/qemu.log 2>&1 &
pid=$!
mon() { printf '%s\n' "$1" | nc -w 2 127.0.0.1 $PORT >/dev/null 2>&1; }
typ() { local s="$1" i; for ((i=0; i<${#s}; i++)); do mon "sendkey ${s:i:1}"; done; }
click() { ~/serialenv/bin/python tools/qclick.py $PORT "$@" >> $OUT/clicks.log 2>&1; }
STEPS=test/scen_$SCEN.txt
for ((t=5; t<=SECS; t+=5)); do
  sleep 5
  (( t == 25 || t == 40 )) && mon "sendkey ret"
  if (( t == 55 )); then     # boot time varies: hold the clock here until the app's window is up (white, not desktop)
    for ((w=0; w<30; w++)); do
      mon "screendump $PWD/$W/probe.ppm"; sleep 1
      ~/serialenv/bin/python -c "
import sys; from PIL import Image
sys.exit(0 if Image.open('$W/probe.ppm').convert('L').getpixel((300, 300)) > 240 else 1)" && break
      sleep 4
    done
    sleep 5                  # and let it settle
  fi
  if [ -f $STEPS ]; then
    while read -r st what a b; do
      [ "$st" = "$t" ] || continue
      case $what in key) mon "sendkey $a";; type) typ "$a";; click) click $a $b;; sq) click sq $a $b;; drag) click drag $a $b;; mon) mon "$a $b";; sh) eval "$a $b";; esac
    done < $STEPS
  fi
  if (( t >= 60 )); then
    mon "screendump $PWD/$OUT/screen_${t}s.ppm"; sleep 1
    sips -s format png $OUT/screen_${t}s.ppm --out $OUT/screen_${t}s.png >/dev/null 2>&1 && rm -f $OUT/screen_${t}s.ppm
  fi
done
mon quit; sleep 2; kill -9 $pid 2>/dev/null; [ -n "$sim" ] && kill $sim 2>/dev/null; wait 2>/dev/null
[ -f $OUT/fujisim.log ] && cat $OUT/fujisim.log
