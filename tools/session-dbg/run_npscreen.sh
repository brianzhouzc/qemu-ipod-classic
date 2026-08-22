#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT; export IREMOTE_OUT=$OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
export DISPLAY=:0 PULSE_SERVER=unix:/mnt/wslg/PulseServer
pkill -9 -x qmus 2>/dev/null; pkill -9 -x ipod-hle-run 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1 HLE_AUDIO_SUSTAIN=1
export HLE_AUDIO_DATAPATH=1 HLE_AUDIO_DISK=/home/dmontero/6g/Emulti.img
export DISPTRACE=1
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -monitor "tcp:127.0.0.1:4455,server,nowait" -L build/pc-bios \
  >$OUT/qmus_scr.out 2>$OUT/qmus_scr.err &
echo "launched pid $!"
sleep 50
# reliable run_dksnap-style navigation to Shuffle Songs + play
$IR wake e >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name Zn1 >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:2 Zn2 >/dev/null 2>&1
for i in 1 2 3 4 5 6 7 8; do $IR seq e:120 wait:1 ret:250 wait:2 z >/dev/null 2>&1; done
echo "=== PLAY sent; capturing past 15s ==="
sleep 3;  $IR shot ZNP_t03 >/dev/null 2>&1
sleep 7;  $IR shot ZNP_t10 >/dev/null 2>&1
sleep 8;  $IR shot ZNP_t18 >/dev/null 2>&1   # past the 15s reboot point
sleep 12; $IR shot ZNP_t30 >/dev/null 2>&1
echo "=== redirect / suppressed events ==="
grep -aE "SUPPRESSED|redirecting|software reset" $OUT/qmus_scr.err | tail -6
echo "=== DISPTRACE: source-address writes (screen presenting) ==="
grep -aE "SOURCE ADDRESS|disp" $OUT/qmus_scr.err | tail -8
exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 1 && exec 3<&-
pkill -9 -x qmus 2>/dev/null; echo DONE
