#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/counts.log; rm -f $LOG
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1
PA="nodefault=on,always=on,cap=3,out=$LOG,pc=0x08150d84:StartPlayback"
PA="$PA,pc=0x08123dcc:disp,pc=0x08123284:dstep,pc=0x08123bfc:dwork"
PA="$PA,pc=0x08025ac4:snd9init,pc=0x08025a48:recv9,pc=0x08123970:h12,pc=0x08123cfc:h19"
PA="$PA,pc=0x081235d0:hA,pc=0x08123508:starve,pc=0x08123854:datah"
PA="$PA,pc=0x08016e44:copyfn,pc=0x08016eb4:copy32,pc=0x0808170c:engine,pc=0x0809a74c:mixer"
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS"   -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot   -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234   -plugin "$PLUG,$PA" >$OUT/qmus.out 2>$OUT/qmus.err &
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
echo '=== SUMMARY ==='; sed -n '/PCTRACE summary/,$p' $LOG
echo "resets=$(grep -c 'software reset' $OUT/qmus.err)"
pkill -9 -x qmus 2>/dev/null
echo DONE
