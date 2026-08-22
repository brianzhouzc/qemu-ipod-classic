#!/bin/bash
# Launch retailOS (AUDIOPACE=1, gdb stub), nav to Shuffle Songs, let it deadlock,
# then dump full wait-graph (wgraph.py) + hw state (catch4.gdb). Arg1 = run label.
set +e
LBL=${1:-R}
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1 AUDIOWAV=/home/dmontero/6g/wg_${LBL}.wav
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234 \
  >"$OUT/qmus.out" 2>"$OUT/qmus.err" &
sleep 48
$IR wake e 3000 >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 8 -t 3000 -s 3 --name ${LBL}a 2>&1 | tail -1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 ${LBL}b 2>&1 | tail -1
for t in 1 2 3; do $IR seq e:120 ret:220 wait:1 >/dev/null 2>&1; done
# let it settle into the deadlock (~367ms after SELECT; give it margin)
sleep 4
echo "########## WGRAPH ${LBL} ##########"
timeout 40 gdb-multiarch -q -batch -x ~/dbg/wgraph.py 2>&1 | grep -vE "auto-load|safe-path|No executable|determining|file command|configuration|information about|Auto-loading|E.g.,|Remote deb|Reading|Inferior|warning: A|To enable|To comp|Remote communication|GNU gdb|Copyright|License|This is free|There is NO|and \"show|Type \"|For bug|Find the|^$"
echo "########## HW ${LBL} ##########"
timeout 25 gdb-multiarch -q -batch -x ~/dbg/catch4.gdb 2>&1 | grep -E "PC=|I2S:|DMAC0 CONFIG|ch1 SRC|SM1:"
pkill -9 -x qmus 2>/dev/null; sleep 1
echo "########## DONE ${LBL} resets=$(grep -c "software reset" "$OUT/qmus.err") ##########"
