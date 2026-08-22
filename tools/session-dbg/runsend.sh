#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/send.log; rm -f $LOG
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1
PA="nodefault=on,always=on,cap=3,regcap=300,out=$LOG,pc=0x08150d84:StartPlayback"
PA="$PA,pc=0x0808170c:engine,pc=0x0809a74c:mixer,pc=0x08016e44:copyfn,pc=0x08016eb4:copy32"
PA="$PA,pc=0x08123970:h12,pc=0x08123dcc:disp"
PA="$PA,pc=0x0805c56c:rpcOK"
# engine RPC send+wait (obj in r0, caller in lr):
PA="$PA,regpc=0x0805c518:portRPC"
# server-side port deliver (obj in r0):
PA="$PA,regpc=0x0805c460:portSVR"
# op3 SEND-mbox veneer (arg in r0):
PA="$PA,regpc=0x22004038:op3snd"
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
  grep -aq "08150d84  #" $LOG 2>/dev/null && { echo "StartPlayback try $t"; break; }
done
sleep 16
exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 2 && exec 3<&-
sleep 2
echo "=== COUNTERS ==="; sed -n "/PCTRACE summary/,\$p" $LOG | grep -aE "StartPlayback|engine |mixer|copyfn|copy32|h12 |disp |rpcOK|portRPC|portSVR|op3snd"
echo "=== portRPC callers (lr) + objs (r0) ==="; grep -a portRPC $LOG | grep REGPC | awk "{print \$6, \$7}" | sort | uniq -c | sort -rn | head -20
echo "=== portRPC first 6 / last 4 ==="; grep -a portRPC $LOG|grep REGPC|head -6; echo ...; grep -a portRPC $LOG|grep REGPC|tail -4
echo "=== portSVR callers+objs ==="; grep -a portSVR $LOG|grep REGPC|awk "{print \$6,\$7}"|sort|uniq -c|sort -rn|head
echo "=== op3snd callers+args ==="; grep -a op3snd $LOG|grep REGPC|awk "{print \$6,\$7}"|sort|uniq -c|sort -rn|head
echo "resets=$(grep -c "software reset" $OUT/qmus.err)"
pkill -9 -x qmus 2>/dev/null
echo DONE
