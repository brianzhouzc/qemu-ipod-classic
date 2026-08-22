#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin; OUT=/home/dmontero/6g/remote
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" \
  -d unimp,guest_errors -D "$OUT/unimp.log" \
  >"$OUT/qmus.out" 2>"$OUT/qmus.err" &
sleep 48
$IR wake e 3000 >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 8 -t 3000 -s 3 --name Q01 2>&1 | tail -1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 Q02 2>&1 | tail -1
echo "=== MARK_SELECT $(wc -l < $OUT/unimp.log) ===" 
BEFORE=$(wc -l < "$OUT/unimp.log")
for t in 1 2 3; do $IR seq e:120 ret:220 wait:1 >/dev/null 2>&1; done
sleep 10
$IR raw quit >/dev/null 2>&1; sleep 2; pkill -9 -x qmus 2>/dev/null
echo "=== unimplemented accesses DURING PLAY (after line $BEFORE) ==="
tail -n +$BEFORE "$OUT/unimp.log" | grep -oE "s5l8702\.[a-z0-9-]+|io: (read|write).*addr 0x[0-9a-f]+" | sed -E "s/(0x[0-9a-f]{4})[0-9a-f]{4}/\1xxxx/" | sort | uniq -c | sort -rn | head -40
echo "=== total lines during play: $(( $(wc -l < $OUT/unimp.log) - BEFORE )) ==="
echo "=== distinct io addresses during play ==="
tail -n +$BEFORE "$OUT/unimp.log" | grep -oE "addr 0x3[0-9a-f]+" | sed -E "s/(0x3[0-9a-f]{3})[0-9a-f]+/\1xxx/" | sort | uniq -c | sort -rn | head
echo DONE_UNIMP
