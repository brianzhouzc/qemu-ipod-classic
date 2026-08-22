#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT; export IREMOTE_OUT=$OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/npalive.log; rm -f $LOG
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1 HLE_AUDIO_SUSTAIN=1 HLE_AUDIO_DATAPATH=1 HLE_AUDIO_DISK=/home/dmontero/6g/Emulti.img
PA="nodefault=on,always=on,cap=8,window=6000,out=$LOG"
PA="$PA,pc=0x08150d84:StartPlay,pc=0x081faac8:GotoNP,pc=0x081a168c:NPactivate,pc=0x081a1cfc:NPdraw,pc=0x08016eb4:copy32"
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" \
  -plugin "$PLUG,$PA" >$OUT/qmus_al.out 2>$OUT/qmus_al.err &
echo "launched pid $!"; sleep 52
$IR wake e >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name A1 >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:2 A2 >/dev/null 2>&1
for i in $(seq 1 10); do $IR seq e:120 wait:1 ret:250 wait:2 z >/dev/null 2>&1; if grep -aqE "cpu.*StartPlay " $LOG; then echo "StartPlay confirmed try $i"; break; fi; done
echo "letting it run 40s past play (well past the 15s reset point)..."; sleep 40
echo "=== redirect events ==="; grep -acE "redirecting CPU" $OUT/qmus_al.err; grep -aE "redirecting CPU" $OUT/qmus_al.err | tail -3
echo "=== counters with timestamps (do NP draws continue past the reset?) ==="
grep -aE "cpu[0-9]  (StartPlay|GotoNP|NPactivate|NPdraw|copy32) " $LOG | head -4
echo "... last NPdraw / NPactivate hits (timestamp shows if UI alive late):"
grep -aE "cpu[0-9]  (NPdraw|NPactivate|GotoNP) " $LOG | tail -6
echo "totals:"; for c in StartPlay GotoNP NPactivate NPdraw copy32; do echo "$c: $(grep -acE "cpu.* $c " $LOG)"; done
exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 1 && exec 3<&-
pkill -9 -x qmus 2>/dev/null; echo DONE
