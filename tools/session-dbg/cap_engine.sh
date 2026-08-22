#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin; OUT=/home/dmontero/6g/remote
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234 \
  >"$OUT/qmus.out" 2>"$OUT/qmus.err" &
sleep 48
$IR wake e 3000 >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 8 -t 3000 -s 3 --name W01 2>&1 | tail -1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 W02 2>&1 | tail -1
# start gdb FIRST (it will break at StartPlayback), THEN SELECT so the engine run is caught
( timeout 40 gdb-multiarch -q -batch -x ~/dbg/catch_engine.py 2>&1 | grep -vE "auto-load|safe-path|No executable|determining|file command|configuration|information about|Auto-loading|E.g.,|Remote|Reading|Inferior|deprecated|mi-async|enable execution|disable this|warning: Add|To enable|To comp" ) &
GP=$!
sleep 3
for t in 1 2 3 4; do $IR seq e:120 ret:220 wait:1 >/dev/null 2>&1; done
wait $GP
echo "resets: $(grep -c "software reset" "$OUT/qmus.err")"
pkill -9 -x qmus 2>/dev/null
echo DONE_CAP_ENGINE
