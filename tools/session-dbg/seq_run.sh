#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
pkill -9 gdb-multiarch 2>/dev/null; pkill -9 -x qmus 2>/dev/null; sleep 2
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234 \
  >"$OUT/qmus.out" 2>"$OUT/qmus.err" &
sleep 46
# navigate to language->main then get near Shuffle Songs (do NOT SELECT yet)
$IR wake e 3000 >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 8 -t 3000 -s 3 --name Sa >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 Sb >/dev/null 2>&1
# start the watch-trace gdb in background (it will run continue loop)
( timeout 40 gdb-multiarch -q -batch -x ~/dbg/catch_ex6.gdb > /home/dmontero/6g/ex6.trace 2>/home/dmontero/6g/ex6.err ) &
GPID=$!
sleep 2
# now SELECT shuffle songs (fires StartPlayback) while gdb is watching
for t in 1 2 3; do $IR seq e:120 ret:220 wait:1 >/dev/null 2>&1; done
wait $GPID
pkill -9 -x qmus 2>/dev/null
echo "=== EX6 TRACE ==="
grep -vE "warning|deprecated|mi-async|Hardware|Watchpoint|Old value|New value|safe|auto-load|configuration|E.g|information|section|add-auto|To enable|To compl|Reading|^$" /home/dmontero/6g/ex6.trace | head -80
echo "=== resets: $(grep -c "software reset" "$OUT/qmus.err") ==="
