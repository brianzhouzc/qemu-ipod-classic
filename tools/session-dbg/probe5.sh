#!/bin/bash
# probe5.sh [EXTRA_QEMU_ARGS] -- CLEAN play (no gdb during play), wait 8s after
# StartPlayback for setup to settle, then ONE gdb read of the regime. Env LABEL.
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/probe5.log; rm -f $LOG
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
EXTRA="$*"
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1 AUDIOWAV=/home/dmontero/6g/probe5_${LABEL:-x}.wav
PA="nodefault=on,always=on,cap=50,out=$LOG,pc=0x08150d84:StartPlayback"
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234 \
  $EXTRA -plugin "$PLUG,$PA" >$OUT/qmus.out 2>$OUT/qmus.err &
echo "launched pid $!  extra=[$EXTRA]"
sleep 48
$IR wake e 3000 >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 8 -t 3000 -s 3 --name Pa >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 Pb >/dev/null 2>&1
FIRED=0
for t in 1 2 3 4 5 6 7 8; do
  $IR seq e:120 ret:220 wait:1 >/dev/null 2>&1
  if grep -aq StartPlayback $LOG 2>/dev/null; then echo "StartPlayback try $t"; FIRED=1; break; fi
done
[ $FIRED = 0 ] && echo "NO StartPlayback"
sleep 8
timeout 15 gdb-multiarch -q -batch -x ~/dbg/rd2.gdb 2>&1 | grep -aE 'REGIME'
echo "resets=$(grep -c 'software reset' $OUT/qmus.err)"
python3 tools/ipod_classic/wavcheck.py /home/dmontero/6g/probe5_${LABEL:-x}.wav 2>&1 | grep -E 'VERDICT|non-zero|peak'
pkill -9 -x qmus 2>/dev/null
echo DONE
