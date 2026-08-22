#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT; export IREMOTE_OUT=$OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/npmemw.log; rm -f $LOG
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1 HLE_AUDIO_SUSTAIN=1
export HLE_NP=1 HLE_NP_TRACE=1 HLE_NP_DEC_ONLY=1
RANGE="${RANGE:-0x08888000-0x0888c000}"
PA="nodefault=on,always=on,cap=8,window=6000,out=$LOG"
PA="$PA,pc=0x081e9e70:innerdec,sendex=0x22004138:3:0x6:ex6"
PA="$PA,memw=$RANGE,memcap=40000"
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" \
  -plugin "$PLUG,$PA" >$OUT/qmus_npm.out 2>$OUT/qmus_npm.err &
echo "launched pid $! memw=$RANGE"
sleep 70
$IR wake e >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name M1 >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:2 M2 >/dev/null 2>&1
for i in $(seq 1 12); do
  $IR seq e:120 wait:1 ret:250 wait:2 z >/dev/null 2>&1
  if grep -aqE "cpu[0-9]  (ex6|innerdec) " $LOG 2>/dev/null; then echo "regime-A try $i"; break; fi
done
sleep 8
echo "decode kicks: $(grep -ac "HLE_NP: DECODE" $OUT/qmus_npm.err)  innerdec: $(grep -ac innerdec $LOG)"
echo "=== who writes $RANGE (memw: PC -> addr) ==="
grep -aE "memw|MEMW|store|write" $LOG | head -40
echo "--- raw tail of memw log ---"; grep -aiE "0888|memw" $LOG | tail -30
exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 1 && exec 3<&-
pkill -9 -x qmus 2>/dev/null; echo DONE
