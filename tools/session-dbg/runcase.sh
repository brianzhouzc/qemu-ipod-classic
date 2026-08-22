#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/case.log; rm -f $LOG
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1
PA="nodefault=on,always=on,cap=2,out=$LOG,pc=0x08150d84:StartPlayback"
PA="$PA,pc=0x08123d50:c19_n0,pc=0x08123d64:c19_n1,pc=0x08123d78:c19_n5,pc=0x08123d8c:c19_n6,pc=0x08123d9c:c19_n2347,pc=0x08123dac:c19_n89"
PA="$PA,pc=0x08123bfc:dwork,pc=0x08123284:dstep,pc=0x08123bdc:startdec,pc=0x08123970:h12,pc=0x08123508:rearm"
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234 \
  -plugin "$PLUG,$PA" >$OUT/qmus.out 2>$OUT/qmus.err &
echo "launched pid $!"
sleep 48
$IR wake e 3000 >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 8 -t 3000 -s 3 --name Pa >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 Pb >/dev/null 2>&1
for t in 1 2 3 4 5 6 7 8; do
  $IR seq e:120 ret:220 wait:1 >/dev/null 2>&1
  grep -aq StartPlayback $LOG 2>/dev/null && { echo "StartPlayback try $t"; break; }
done
sleep 14
exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 2 && exec 3<&-
sleep 2
echo "=== CASE COUNTERS ==="; sed -n "/PCTRACE summary/,\$p" $LOG | grep -aE "StartPlayback|c19_|dwork|dstep|startdec|h12|rearm"
echo "resets=$(grep -c software\ reset $OUT/qmus.err)"
pkill -9 -x qmus 2>/dev/null
echo DONE
