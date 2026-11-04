#!/bin/bash
# net_test.sh: the network game in QEMU against a LOCAL copy of the table server (port 8090), never Tim's.
#   bash test/net_test.sh        -> test/out/net/ (screens, fujisim.log, server.log, opponent.log)
# fujisim sends https://lisa-gw1.tailc2f141.ts.net/checkers to http://127.0.0.1:8090; test/opponent.sh plays the
# other seat with curl; test/scen_net.txt is the Mac's side (it stops the server near the end, to show the drive's
# own ERROR words).
set -u
cd "$(dirname "$0")/.."
OUT=test/out/net; mkdir -p $OUT
python3 /Volumes/RetroDev/src/Apple-LisaCheckers/server/ckr_server.py 8090 > $OUT/server.log 2>&1 &
echo $! > test/work/server.pid
sleep 1
bash test/opponent.sh > $OUT/opponent.log 2>&1 &
opp=$!
SCEN=net REWRITE=1 bash test/run_qemu.sh ${1:-300}
curl -s --max-time 3 "http://127.0.0.1:8090/leave?table=c1&player=macxl" >/dev/null 2>&1   # the Mac left (Command-L); make sure
kill $(cat test/work/server.pid) 2>/dev/null; kill $opp 2>/dev/null; wait 2>/dev/null
echo "--- opponent"; cat $OUT/opponent.log
echo "--- the Mac's requests to the server"; grep -c "macxl" $OUT/server.log
