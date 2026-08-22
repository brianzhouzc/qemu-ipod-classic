#!/bin/bash
set +e
cd /mnt/c/Desarrollo/qemu-ios; IR='python3 tools/ipod_classic/iremote.py'
~/dbg/launch_wg.sh
echo 'booting 55s'; sleep 55
$IR wake e 6000 >/dev/null 2>&1; sleep 1
$IR seq w:150 wait:1 w:150 R0 >/dev/null 2>&1
$IR seq e:120 e:120 e:120 e:120 e:120 e:120 e:120 e:120 R1 >/dev/null 2>&1
if ! pgrep -x qmus >/dev/null; then echo 'QMUS DIED during nav'; exit 1; fi
echo 'arming gdb (break engine+StartPlayback), continue'
setsid gdb-multiarch -batch -x ~/dbg/catch_enum.py > /home/dmontero/6g/enum.out 2>&1 &
GDBPID=$!
sleep 3
echo 'reliable SELECT onto shuffle songs'
$IR press ret 250 -u -w e -r 10 -t 2000 -s 3 --name SELg >/dev/null 2>&1 &
DRVPID=$!
for w in $(seq 1 45); do
  if ! kill -0 $GDBPID 2>/dev/null; then echo "gdb finished at w=$w"; break; fi
  sleep 1
done
kill -9 $GDBPID $DRVPID 2>/dev/null
echo '=== ENUM OUTPUT ==='
cat /home/dmontero/6g/enum.out
echo '=== qmus alive? ==='; pgrep -x qmus || echo dead
