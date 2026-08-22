#!/bin/bash
# probe9.sh -- play with IRQDBG=1; measure timer-tick + DMAC0 per-channel TC rates,
# and whether they keep firing INTO the deadlock (delta over 4s while stuck).
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/probe9.log; rm -f $LOG
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1 IRQDBG=1
PA="nodefault=on,always=on,cap=50,out=$LOG,pc=0x08150d84:StartPlayback"
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234 \
  -plugin "$PLUG,$PA" >$OUT/qmus.out 2>$OUT/qmus.err &
echo "launched pid $!"
sleep 48
$IR wake e 3000 >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 8 -t 3000 -s 3 --name Pa >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 Pb >/dev/null 2>&1
for t in 1 2 3 4 5 6; do $IR seq e:120 ret:220 wait:1 >/dev/null 2>&1; grep -aq StartPlayback $LOG 2>/dev/null && { echo "StartPlayback try $t"; break; }; done
sleep 8
echo "=== IRQDBG snapshot A (at deadlock) -- last count per line ==="
grep -a IRQDBG $OUT/qmus.err | awk '{print $2, $3}' | sort | uniq -c | tail -20
echo "--- last 6 IRQDBG lines A ---"; grep -a IRQDBG $OUT/qmus.err | tail -6
A=$(grep -ac IRQDBG $OUT/qmus.err)
sleep 4
B=$(grep -ac IRQDBG $OUT/qmus.err)
echo "=== IRQDBG delta over 4s while STUCK: $A -> $B (new=$((B-A))) ==="
echo "--- which channels/timers fired in that 4s window ---"
grep -a IRQDBG $OUT/qmus.err | tail -n +$((A+1)) | sed -E 's/TC#[0-9]+//; s/tick#[0-9]+//' | sort | uniq -c
echo "--- regime ---"
timeout 15 gdb-multiarch -q -batch -x ~/dbg/rd2.gdb 2>&1 | grep -aE 'REGIME'
echo "resets=$(grep -c 'software reset' $OUT/qmus.err)"
pkill -9 -x qmus 2>/dev/null
echo DONE
