#!/bin/bash
# probe4.sh -- trigger play, attach gdb watchpoints on exchange-6 + mbox0x52-seq,
# capture the signal/wait sequence for ~22s to find the lost-wakeup cycle.
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/probe4.log; rm -f $LOG /home/dmontero/6g/ex6.out
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1
PA="nodefault=on,always=on,cap=50,out=$LOG,pc=0x08150d84:StartPlayback"
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234 \
  -plugin "$PLUG,$PA" >$OUT/qmus.out 2>$OUT/qmus.err &
echo "launched pid $!  (boot 48s)"
sleep 48
$IR wake e 3000 >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 8 -t 3000 -s 3 --name Pa 2>&1 | tail -1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 Pb 2>&1 | tail -1
for t in 1 2 3 4 5 6; do
  $IR seq e:120 ret:220 >/dev/null 2>&1
  if grep -aq StartPlayback $LOG 2>/dev/null; then echo "StartPlayback FIRED try $t -- attaching watch"; break; fi
done
# attach watchpoints, capture ~22s
timeout 24 gdb-multiarch -q -batch -x ~/dbg/catch_ex6b.gdb > /home/dmontero/6g/ex6.out 2>/dev/null
echo "=== exchange-6 / mbox52 write sequence (first 60) ==="
grep -aE 'EX6|M52w6' /home/dmontero/6g/ex6.out | head -60
echo "=== counts ==="; grep -ac EX6 /home/dmontero/6g/ex6.out; grep -ac M52w6 /home/dmontero/6g/ex6.out
echo "=== last 15 events ==="; grep -aE 'EX6|M52w6' /home/dmontero/6g/ex6.out | tail -15
echo "=== resets=$(grep -c 'software reset' $OUT/qmus.err) ==="
pkill -9 -x qmus 2>/dev/null
echo DONE
