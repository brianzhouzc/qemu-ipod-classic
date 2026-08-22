#!/bin/bash
# probe2.sh -- trigger play, then IMMEDIATELY read codec type + wait-graph via
# gdb (reliable, non-invasive) before the stall-watchdog reset fires.
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/probe2.log; rm -f $LOG /home/dmontero/6g/iram2.bin
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1
PA="nodefault=on,always=on,cap=50,out=$LOG,pc=0x08150d84:StartPlayback,pc=0x0808170c:engine"
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234 \
  -plugin "$PLUG,$PA" >$OUT/qmus.out 2>$OUT/qmus.err &
echo "launched pid $!  (boot 48s)"
sleep 48
$IR wake e 3000 >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 8 -t 3000 -s 3 --name Pa 2>&1 | tail -1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 Pb 2>&1 | tail -1
echo "--- select + immediate gdb read ---"
GOT=0
for t in 1 2 3 4 5 6 7 8; do
  $IR seq e:120 ret:220 wait:1 >/dev/null 2>&1
  if grep -aq StartPlayback $LOG 2>/dev/null; then
    echo "StartPlayback FIRED after try $t -- reading NOW"
    timeout 20 gdb-multiarch -q -batch -x ~/dbg/rd.gdb 2>&1 | grep -avE 'auto-load|safe path|No executable|Reading|warning: A|Remote deb|GNU gdb|Copyright|License|free soft|NO WARRANTY|show copy|Type .show|For bug|Find the|manual|^\[Inferior|Detaching|Ending remote'
    GOT=1
    break
  fi
done
[ $GOT = 0 ] && echo "StartPlayback NEVER fired (play did not trigger)"
echo "--- engine hits (real, filtered) ---"; grep -ac 'engine' $LOG
echo "=== resets=$(grep -c 'software reset' $OUT/qmus.err) ==="
pkill -9 -x qmus 2>/dev/null
echo DONE
