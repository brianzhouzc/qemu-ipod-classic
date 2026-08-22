#!/bin/bash
set -e
cd /mnt/c/Desarrollo/qemu-ios
IR="python3 tools/ipod_classic/iremote.py"
echo "=== relaunch ==="
~/dbg/playgdb.sh /home/dmontero/6g/Emulti.img 1234 4455
sleep 48
echo "=== nav to Shuffle Songs ==="
$IR wake e 3000 >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 12 -t 3000 -s 4 --name H01-main 2>&1 | tail -1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:2 H02-shuf 2>&1 | tail -1
echo "=== SELECT Shuffle Songs (start playback) ==="
for t in 1 2 3; do $IR seq e:120 ret:220 wait:1 >/dev/null 2>&1; done
echo "=== wait 4s for decode to block/settle, then attach ==="
sleep 4
timeout 40 gdb-multiarch -q -batch -x ~/dbg/enum.py 2>&1 | grep -v "auto-load\|safe-path\|No executable\|determining\|file command\|configuration file\|information about\|Auto-loading\|E.g.," 
echo "=== resets in this run ==="; grep -c "software reset" ~/6g/remote/qmus.err 2>/dev/null || echo 0
