#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT; export IREMOTE_OUT=$OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/npc.log; rm -f $LOG
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmus 2>/dev/null; pkill -9 -x ipod-hle-run 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1 HLE_AUDIO_SUSTAIN=1
export HLE_AUDIO_DATAPATH=1 HLE_AUDIO_DISK=/home/dmontero/6g/Emulti.img
export HLE_NP=1 HLE_NP_TRACE=1
PA="nodefault=on,always=on,cap=4,window=4000,out=$LOG,pc=0x08150d84:SP,pc=0x08016eb4:copy32,pc=0x08016ecc:copySKIP,sendex=0x22004138:3:0x6:ex6"
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234 \
  -plugin "$PLUG,$PA" >$OUT/qmus_npc.out 2>$OUT/qmus_npc.err &
echo "launched pid $!"
sleep 52
$IR wake e >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name K1 >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:2 K2 >/dev/null 2>&1
for i in $(seq 1 12); do
  $IR seq e:120 wait:1 ret:250 wait:2 z >/dev/null 2>&1
  if grep -aqE "cpu[0-9]  (copySKIP|copy32) " $LOG 2>/dev/null; then echo "render confirmed try $i"; break; fi
done
sleep 6
echo "=== copy args during my engine calls ==="
timeout 40 gdb-multiarch -q -batch -x ~/dbg/catch_copy.gdb 2>&1 | grep -aE "COPY #|src_base"
exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 1 && exec 3<&-
pkill -9 -x qmus 2>/dev/null; echo DONE
