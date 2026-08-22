#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/trig; mkdir -p $OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/trig.log; rm -f $LOG
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmust 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmust"
export AUDIOPACE=1
PA="nodefault=on,always=on,cap=3,regcap=4000,out=$LOG,pc=0x08150d84:StartPlayback"
PA="$PA,pc=0x0808170c:engine,pc=0x08016eb4:copy32,pc=0x08123970:h12,pc=0x08123dcc:disp"
PA="$PA,regpc=0x22004138:kspost"
PA="$PA,regpc=0x22001060:sww"
PA="$PA,regpc=0x22003f7c:selmbox"
setsid "$ROOT/build/qmust" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4456,server,nowait" \
  -plugin "$PLUG,$PA" >$OUT/q.out 2>$OUT/q.err &
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
exec 3<>/dev/tcp/127.0.0.1/4456 && echo quit >&3 && sleep 2 && exec 3<&-
sleep 2
echo "=== COUNTERS ==="; sed -n "/PCTRACE summary/,\$p" $LOG | grep -aE "StartPlayback|engine|copy32|h12|disp"
echo "=== kspost opcode(r2) dist ==="; grep -a kspost $LOG | grep -aoE "r2=0x[0-9a-f]+" | sort | uniq -c | sort -rn
echo "=== kspost index(r3) dist ==="; grep -a kspost $LOG | grep -aoE "r3=0x[0-9a-f]+" | sort | uniq -c | sort -rn
echo "=== kspost total ==="; grep -ac kspost $LOG
echo "=== selmbox r0 (mailbox #) dist ==="; grep -a selmbox $LOG | grep -aoE "r0=0x[0-9a-f]+" | sort | uniq -c | sort -rn
echo "=== selmbox r0==0x52: LR attribution ==="; grep -a selmbox $LOG | grep -a "r0=0x00000052" | grep -aoE "lr=0x[0-9a-f]+" | sort | uniq -c
echo "=== selmbox r0==0x09: LR attribution ==="; grep -a selmbox $LOG | grep -a "r0=0x00000009" | grep -aoE "lr=0x[0-9a-f]+" | sort | uniq -c
echo "=== sww total ==="; grep -ac sww $LOG
echo "=== sww sample (first 10) ==="; grep -a sww $LOG | head -10
echo "=== sww r2 (mbox ptr) dist ==="; grep -a sww $LOG | grep -aoE "r2=0x[0-9a-f]+" | sort | uniq -c | sort -rn | head
echo "resets=$(grep -c "software reset" $OUT/q.err)"
pkill -9 -x qmust 2>/dev/null
echo DONE
