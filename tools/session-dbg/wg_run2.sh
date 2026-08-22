#!/bin/bash
# Same as wg_run but with pctrace plugin markers to see WHAT fired each run.
set +e
LBL=${1:-R}
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
LOG=/home/dmontero/6g/wg2_${LBL}.log; rm -f $LOG
PA="nodefault=on,always=on,cap=6"
PA="$PA,pc=0x081fa9e4:ShuffleSel,pc=0x08150d84:StartPlayback,pc=0x08024798:playA"
PA="$PA,pc=0x0808170c:audio_engine,pc=0x08016eb4:copyREALPCM,memwpc=0x08016eb4:copyPCM,out=$LOG"
export AUDIOPACE=1
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234 \
  -plugin "$PLUG,$PA" >"$OUT/qmus.out" 2>"$OUT/qmus.err" &
sleep 48
$IR wake e 3000 >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 8 -t 3000 -s 3 --name ${LBL}a >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 ${LBL}b >/dev/null 2>&1
for t in 1 2 3; do $IR seq e:120 ret:220 wait:1 >/dev/null 2>&1; done
sleep 4
echo "=== PLUGIN MARKERS ${LBL} ==="
sed -n "/PCTRACE summary/,\$p" $LOG 2>/dev/null | grep -E "ShuffleSel|StartPlayback|playA|audio_engine|copyREALPCM|copyPCM"
echo "=== GDB t20/t25/t2a + sema6 + mbox52 ${LBL} ==="
timeout 40 gdb-multiarch -q -batch -x ~/dbg/wgraph.py 2>&1 | grep -E "ATTACH pc|^t20 |^t25 |^t2a |mbox\[0x52\]|06:00000020|06:ffffffff"
pkill -9 -x qmus 2>/dev/null; sleep 1
echo "=== DONE ${LBL} ==="
