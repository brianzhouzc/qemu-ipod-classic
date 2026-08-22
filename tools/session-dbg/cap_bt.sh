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
$IR press ret 250 -u -w e --region right -r 8 -t 3000 -s 3 --name V01 2>&1 | tail -1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 V02 2>&1 | tail -1
for t in 1 2 3; do $IR seq e:120 ret:220 wait:1 >/dev/null 2>&1; done
sleep 5
timeout 30 gdb-multiarch -q -batch -x ~/dbg/enum_bt.py 2>&1 | grep -vE "auto-load|safe-path|No executable|determining|file command|configuration|information about|Auto-loading|E.g.,|Remote|Reading|Inferior|deprecated|mi-async|enable execution|disable this"
echo "resets: $(grep -c "software reset" "$OUT/qmus.err")"
pkill -9 -x qmus 2>/dev/null
echo DONE_CAP_BT
