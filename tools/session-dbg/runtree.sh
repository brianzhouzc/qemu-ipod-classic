#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/tree.log; rm -f $LOG
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1
PA="nodefault=on,window=250,out=$LOG,range=0x08050000-0x08360000"
PA="$PA,pc=0x08150d84:StartPlayback,trig=0x08123284"
PA="$PA,pc=0x08123bfc:dwork,pc=0x0809a74c:mixer,pc=0x08016eb4:copy32,pc=0x08016e44:copyfn"
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
sleep 10
exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 2 && exec 3<&-
sleep 2
echo '=== RANGE PCs distinct (decode tree) count ==='; grep -a RANGE $LOG | wc -l
echo '=== RANGE PCs in codec 0x0831/0x0832/0x0833 (AAC math?) ==='; grep -a RANGE $LOG | grep -oE '@0x083[0-9a-f]{5}' | sort -u | head -40
echo '=== RANGE PCs in 0x0812 (decoder cluster) ==='; grep -a RANGE $LOG | grep -oE '@0x0812[0-9a-f]{4}' | sort -u | head -50
echo '=== SUMMARY ==='; sed -n '/PCTRACE summary/,$p' $LOG
echo "resets=$(grep -c 'software reset' $OUT/qmus.err)"
pkill -9 -x qmus 2>/dev/null
echo DONE
