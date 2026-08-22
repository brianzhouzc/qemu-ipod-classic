#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/multi5.log; rm -f $LOG
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1
PA="nodefault=on,always=on,cap=50,regcap=900,out=$LOG,pc=0x08150d84:StartPlayback"
PA="$PA,sendex=0x22004178:0:0x6:sig2tag6"
PA="$PA,sendex=0x22004138:3:0x6:t20recv"
PA="$PA,sendex=0x22004058:0:0x1:snd5A,sendex=0x220040a8:0:0x1:snd5B"
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
sleep 12
echo "=== match counts by label ==="
grep -aoE "REGPC (sig2tag6|t20recv|snd5A|snd5B)" $LOG | awk "{print \$2}" | sort | uniq -c
echo "=== sig2tag6 (waker) first 3 + last 6 ==="; grep -a "sig2tag6" $LOG | grep REGPC | head -3; echo ...; grep -a "sig2tag6" $LOG | grep REGPC | tail -6
echo "=== t20recv first 3 + last 4 ==="; grep -a "t20recv" $LOG | grep REGPC | head -3; echo ...; grep -a "t20recv" $LOG | grep REGPC | tail -4
echo "=== StartPlayback marker line ==="; grep -a StartPlayback $LOG | grep PCTRACE | head -1
echo "resets=$(grep -c "software reset" $OUT/qmus.err)"
pkill -9 -x qmus 2>/dev/null
echo DONE
