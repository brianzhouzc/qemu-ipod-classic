#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/aac.log; rm -f $LOG
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1
PA="nodefault=on,always=on,cap=3,out=$LOG,pc=0x08150d84:StartPlayback"
PA="$PA,pc=0x08123dcc:disp,pc=0x08123970:h12,pc=0x08123bfc:dwork,pc=0x081239bc:gateconsume"
PA="$PA,pc=0x08016eb4:copy32,pc=0x080578fc:actorpost"
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS"   -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot   -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234   -plugin "$PLUG,$PA" >$OUT/qmus.out 2>$OUT/qmus.err &
echo "launched pid $!"
sleep 50
$IR wake e 3000 >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 4 --name Pmain >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 Pscroll >/dev/null 2>&1
$IR press ret 300 -u -w q,e --region right -r 5 -t 1800 -s 3 --name Psel >/dev/null 2>&1
echo "select done"
sleep ${1:-4}
echo "=== live decode counters (pre-attach snapshot via log) ==="
grep -aE 'disp|h12|dwork|gateconsume|actorpost' $LOG | grep -a 'PCTRACE +' | tail -3
echo "=== GDB PROBE ==="
timeout 60 gdb-multiarch -q -batch -x ~/dbg/aac_probe.py 2>&1 | grep -avE 'auto-load|safe path|No executable|Reading|warning: A|Remote deb|GNU gdb|Copyright|License|free soft|NO WARRANTY|show copy|Type .show|For bug|Find the|manual|Detaching|Ending remote|^\[Inferior|determining exec|configuration file|disable this|information about'
exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 2 && exec 3<&-
sleep 2
echo "=== pctrace counters (final) ==="
sed -n '/PCTRACE summary/,$p' $LOG | head -12
echo "resets=$(grep -c 'software reset' $OUT/qmus.err)"
pkill -9 -x qmus 2>/dev/null
echo DONE
