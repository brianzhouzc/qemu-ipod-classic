#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin; OUT=/home/dmontero/6g/remote
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1 SM1TRACE=1
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" \
  >"$OUT/qmus.out" 2>"$OUT/sm1.log" &
sleep 48
$IR wake e 3000 >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 8 -t 3000 -s 3 --name Z01 2>&1 | tail -1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 Z02 2>&1 | tail -1
echo "MARK_SELECT" >> "$OUT/sm1.log"
for t in 1 2 3; do $IR seq e:120 ret:220 wait:1 >/dev/null 2>&1; done
sleep 8
$IR raw quit >/dev/null 2>&1; sleep 2; pkill -9 -x qmus 2>/dev/null
echo "=== SM1 accesses AFTER select (play phase) ==="
awk "/MARK_SELECT/{m=1} m&&/SM1TRACE/{print}" "$OUT/sm1.log" | sed -E "s/pc~0x[0-9a-f]+//" | sort | uniq -c | sort -rn | head -40
echo "=== total SM1 accesses after select: $(awk "/MARK_SELECT/{m=1} m&&/SM1TRACE/{c++} END{print c}" "$OUT/sm1.log") ==="
echo "=== resets: $(grep -c "software reset" "$OUT/qmus.err" 2>/dev/null) ==="
echo DONE_SM1
