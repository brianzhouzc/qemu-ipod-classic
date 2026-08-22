#!/bin/bash
# probe8.sh -- low-perturbation breakpoints on codec-setup / gate-set / engine
# gate-read, to see if the OUTPUT engine ever gets a codec bound (gate=1).
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/probe8.log; rm -f $LOG /home/dmontero/6g/setup.out
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1
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
# attach BEFORE selecting so we catch setup as it happens
( timeout 30 gdb-multiarch -q -batch -x ~/dbg/catch_setup.gdb > /home/dmontero/6g/setup.out 2>/dev/null ) &
GP=$!
sleep 2
for t in 1 2 3 4 5 6; do $IR seq e:120 ret:220 wait:1 >/dev/null 2>&1; grep -aq StartPlayback $LOG 2>/dev/null && { echo "StartPlayback try $t"; break; }; done
wait $GP
echo "=== setup breakpoint hits ==="
grep -aE 'CODEC_SETUP|GATE_SET|ENGINE_GATE_READ' /home/dmontero/6g/setup.out | sort | uniq -c | head -30
echo "=== distinct engine gate/type seen ==="
grep -a ENGINE_GATE_READ /home/dmontero/6g/setup.out | sort | uniq -c | tail -8
echo "resets=$(grep -c 'software reset' $OUT/qmus.err)"
pkill -9 -x qmus 2>/dev/null
echo DONE
