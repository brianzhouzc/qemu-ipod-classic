#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/trig2; mkdir -p $OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/trig2.log; rm -f $LOG
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmus2 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus2"
export AUDIOPACE=1
PA="nodefault=on,always=on,cap=3,regcap=200,out=$LOG,pc=0x08150d84:StartPlayback"
PA="$PA,pc=0x0808170c:engine,pc=0x08016eb4:copy32,pc=0x08123970:h12,pc=0x08123dcc:disp,pc=0x22006574:codecsetup"
PA="$PA,regpc=0x22004138:kspost"
PA="$PA,sendex=0x22003f7c:0:0x52:sel52"
PA="$PA,sendex=0x22003f7c:0:0x9:sel09"
setsid "$ROOT/build/qmus2" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4457,server,nowait" \
  -plugin "$PLUG,$PA" >$OUT/q.out 2>$OUT/q.err &
echo "launched pid $!"
sleep 55
$IR wake e 3000 >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 8 -t 3000 -s 3 --name Pa >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 Pb >/dev/null 2>&1
for t in $(seq 1 12); do
  $IR seq e:120 ret:220 wait:1 >/dev/null 2>&1
  grep -aqE "REGPC.*kspost.*r3=0x00000006" $LOG 2>/dev/null && { echo "ex6 post seen try $t"; break; }
done
sleep 18
exec 3<>/dev/tcp/127.0.0.1/4457 && echo quit >&3 && sleep 2 && exec 3<&-
sleep 2
echo "=== COUNTERS ==="; sed -n "/PCTRACE summary/,\$p" $LOG | grep -aE "StartPlayback|engine|copy32|h12|disp|codecsetup|sel52|sel09"
echo "=== kspost opcode(r2) dist ==="; grep -a kspost $LOG | grep REGPC | grep -aoE "r2=0x[0-9a-f]+" | sort | uniq -c | sort -rn
echo "=== kspost index(r3) dist ==="; grep -a kspost $LOG | grep REGPC | grep -aoE "r3=0x[0-9a-f]+" | sort | uniq -c | sort -rn
echo "=== kspost total (REGPC lines) ==="; grep -a kspost $LOG | grep -c REGPC
echo "=== sel52 matches + LR ==="; grep -a sel52 $LOG | grep REGPC | grep -aoE "lr=0x[0-9a-f]+" | sort | uniq -c
echo "=== sel52 count ==="; grep -a sel52 $LOG | grep -c REGPC
echo "=== sel09 matches + LR (first 8 distinct) ==="; grep -a sel09 $LOG | grep REGPC | grep -aoE "lr=0x[0-9a-f]+" | sort | uniq -c | sort -rn | head -8
echo "=== sel09 count ==="; grep -a sel09 $LOG | grep -c REGPC
echo "=== sel09 sample REGPC lines (first 4) ==="; grep -a sel09 $LOG | grep REGPC | head -4
echo "resets=$(grep -c "software reset" $OUT/q.err)"
pkill -9 -x qmus2 2>/dev/null
echo DONE
