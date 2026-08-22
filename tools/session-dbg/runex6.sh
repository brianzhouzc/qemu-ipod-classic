#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/ex6.log; rm -f $LOG
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1
PA="nodefault=on,always=on,cap=3,regcap=700,out=$LOG,pc=0x08150d84:StartPlayback"
PA="$PA,pc=0x0808170c:engine,pc=0x08016eb4:copy32,pc=0x08123970:h12,pc=0x08123dcc:disp"
PA="$PA,sendex=0x22004138:3:0x6:ex6post"
PA="$PA,sendex=0x22003f7c:0:0x52:sel52"
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
sleep 16
exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 2 && exec 3<&-
sleep 2
echo "=== PC COUNTERS ==="; sed -n "/PCTRACE summary/,\$p" $LOG | grep -aE "StartPlayback|engine|copy32|h12|disp|ex6post|sel52|watch"
echo "=== ex6 posts: opcode (r2) distribution ==="; grep -a ex6post $LOG | grep REGPC | grep -aoE "r2=0x[0-9a-f]+" | sort | uniq -c
echo "=== ex6 posts: by (LR) ==="; grep -a ex6post $LOG | grep REGPC | grep -aoE "lr=0x[0-9a-f]+" | sort | uniq -c | sort -rn
echo "=== ex6 posts: first 10 (time,lr,r0,r2,r3) ==="; grep -a ex6post $LOG | grep REGPC | head -10
echo "=== ex6 posts: last 6 ==="; grep -a ex6post $LOG | grep REGPC | tail -6
echo "=== ex6 total matches ==="; grep -a "ex6post" $LOG | grep -c REGPC
echo "resets=$(grep -c "software reset" $OUT/qmus.err)"
pkill -9 -x qmus 2>/dev/null
echo DONE
