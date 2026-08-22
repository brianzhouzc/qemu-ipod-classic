#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/multi3.log; rm -f $LOG
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1
# sendex REG=0 (r0=object index) WANT=6, on both send wrappers + both receive wrappers
PA="nodefault=on,always=on,cap=50,regcap=800,out=$LOG,pc=0x08150d84:StartPlayback"
PA="$PA,sendex=0x22004058:0:0x6:sndA,sendex=0x220040a8:0:0x6:sndB"
PA="$PA,sendex=0x22004138:0:0x6:rcvC,sendex=0x22004100:0:0x6:rcvD"
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234 \
  -plugin "$PLUG,$PA" >$OUT/qmus.out 2>$OUT/qmus.err &
echo "launched pid $!"
sleep 48
$IR wake e 3000 >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 8 -t 3000 -s 3 --name Pa >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 Pb >/dev/null 2>&1
for t in 1 2 3 4 5 6 7 8; do
  $IR seq e:120 ret:220 wait:1 >/dev/null 2>&1
  grep -aq StartPlayback $LOG 2>/dev/null && { echo "StartPlayback try $t"; break; }
done
sleep 10
echo "=== send/recv to index 6: counts by label ==="
grep -aoE "(sndA|sndB|rcvC|rcvD)" $LOG | sort | uniq -c
echo "=== last 30 sendex lines ==="
grep -aE "sndA|sndB|rcvC|rcvD" $LOG | tail -30
echo "=== summary block ==="
grep -aE "reg-read|sendex|matches|hits=" $LOG | tail -20
echo "resets=$(grep -c "software reset" $OUT/qmus.err)"
pkill -9 -x qmus 2>/dev/null
echo DONE
