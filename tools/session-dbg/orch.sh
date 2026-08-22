#!/bin/bash
set +e
cd /mnt/c/Desarrollo/qemu-ios; IR='python3 tools/ipod_classic/iremote.py'
~/dbg/launch_wg.sh
echo 'booting 55s'; sleep 55
$IR wake e 6000 >/dev/null 2>&1
sleep 1
# navigate to root then last item (shuffle songs) - minimal
$IR seq w:150 wait:1 w:150 R0 >/dev/null 2>&1
$IR seq e:120 e:120 e:120 e:120 e:120 e:120 e:120 e:120 R1 >/dev/null 2>&1
python3 tools/ipod_classic/pixcount.py /home/dmontero/6g/remote/R1.ppm 2>/dev/null | grep non-black
if ! pgrep -x qmus >/dev/null; then echo 'QMUS DIED during nav'; exit 1; fi
echo 'launching gdb (break engine), then SELECT'
# gdb in background: will halt at engine
setsid gdb-multiarch -batch -x ~/dbg/catch_enum.py > /home/dmontero/6g/enum.out 2>&1 &
GDBPID=$!
sleep 3   # let gdb attach + set bp + continue
# now send the decisive SELECT
$IR key ret 250 >/dev/null 2>&1
$IR key ret 250 >/dev/null 2>&1
# wait for gdb to finish (engine hit -> enumerate -> quit)
for w in $(seq 1 40); do
  if ! kill -0 $GDBPID 2>/dev/null; then break; fi
  sleep 1
done
kill -9 $GDBPID 2>/dev/null
echo '=== ENUM OUTPUT ==='
cat /home/dmontero/6g/enum.out
