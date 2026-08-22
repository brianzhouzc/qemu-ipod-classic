#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios
OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
cd $ROOT
IR="python3 tools/ipod_classic/iremote.py"
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
PA="nodefault=on,always=on,cap=20,pc=0x08150d84:StartPlayback,pc=0x08024798:playA,pc=0x22002dc0:ClockPowerRoutine,pc=0x22002e20:EIC_SPIN,pc=0x22003b0c:WFI_IDLE,pc=0x22001ca4:KS_ISRSignal,out=/home/dmontero/6g/pctrace.log"
export AUDIOPACE=1
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" \
  -plugin "$PLUG,$PA" >"$OUT/qmus.out" 2>"$OUT/qmus.err" &
echo "launched, booting..."
sleep 48
$IR wake e 3000 >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 8 -t 3000 -s 3 --name T01 2>&1 | tail -1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 T02 2>&1 | tail -1
echo "SELECT + play 18s"
for t in 1 2 3; do $IR seq e:120 ret:220 wait:1 >/dev/null 2>&1; done
sleep 18
$IR raw quit >/dev/null 2>&1
sleep 2
pkill -9 -x qmus 2>/dev/null
echo "=== resets: $(grep -c "software reset" "$OUT/qmus.err") ==="
echo "=== pctrace summary ==="
sed -n "/PCTRACE summary/,\$p" /home/dmontero/6g/pctrace.log
echo "=== first StartPlayback/playA/ClockPower lines ==="
grep -E "StartPlayback|playA|ClockPowerRoutine|EIC_SPIN" /home/dmontero/6g/pctrace.log | head -8
echo "DONE_TRACE_RUN"
