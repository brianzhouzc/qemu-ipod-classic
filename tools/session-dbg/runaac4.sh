#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
export IREMOTE_OUT=$OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/aac.log; rm -f $LOG
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1
PA="nodefault=on,always=on,cap=6,window=4000,out=$LOG"
PA="$PA,pc=0x081fa9e4:Shuffle,pc=0x08150d84:StartPlayback"
PA="$PA,pc=0x08123dcc:disp,pc=0x08123970:h12,pc=0x08123bfc:dwork,pc=0x081239bc:gateconsume,pc=0x08016eb4:copy32"
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS"   -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot   -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234   -plugin "$PLUG,$PA" >$OUT/qmus.out 2>$OUT/qmus.err &
echo "launched pid $!"
sleep 52
$IR wake e >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name L1 >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:2 L2 >/dev/null 2>&1
CONF=0
for i in 1 2 3 4 5 6 7 8; do
  $IR seq e:120 wait:1 ret:250 wait:2 z >/dev/null 2>&1
  if grep -aqE 'cpu[0-9]  (dwork|h12) ' $LOG 2>/dev/null; then echo "DECODE CONFIRMED try $i"; CONF=1; break; fi
  if grep -aqE 'cpu[0-9]  Shuffle ' $LOG 2>/dev/null && [ $i -ge 3 ]; then echo "shuffle handler landed try $i (waiting decode)"; fi
done
# screenshot for visual confirmation of Now Playing
$IR shot NowPlaying >/dev/null 2>&1
echo "CONF=$CONF; NowPlaying nonblack:"; grep -a 'NowPlaying:' /dev/stdin 2>/dev/null
echo "=== decode hit lines (real, cpu-tagged) ==="
grep -aE 'cpu[0-9]  (StartPlayback|Shuffle|disp|h12|dwork|gateconsume|copy32) ' $LOG | head -24
echo "=== 5s free-run then PROBE ==="; sleep 5
timeout 60 gdb-multiarch -q -batch -x ~/dbg/aac_probe2.py 2>&1 | grep -avE 'auto-load|safe path|No executable|Reading|warning: A|Remote deb|GNU gdb|Copyright|License|free soft|NO WARRANTY|show copy|Type .show|For bug|Find the|manual|Detaching|Ending remote|^\[Inferior|determining exec|configuration file|disable this|information about'
exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 2 && exec 3<&-
sleep 2
echo "=== pctrace counters (final) ==="
sed -n '/PCTRACE summary/,$p' $LOG | head -12
echo "resets=$(grep -c 'software reset' $OUT/qmus.err)"
pkill -9 -x qmus 2>/dev/null
echo DONE
