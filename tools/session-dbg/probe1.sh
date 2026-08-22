#!/bin/bash
# probe1.sh -- one instrumented play: read the CORRECTED codec-type address
# (state=0x220106dc, type +0x58 = 0x22010734), measure real-PCM copy 0x08016eb4,
# and snapshot the wait-graph. Non-invasive monitor reads (xp).
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/probe1.log; rm -f $LOG
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1 AUDIOWAV=/home/dmontero/6g/probe1.wav
PA="nodefault=on,always=on,cap=4000,out=$LOG"
PA="$PA,pc=0x08150d84:StartPlayback,pc=0x0808170c:engine,pc=0x08016e44:copyPCM,pc=0x08016eb4:copy32REAL,pc=0x08016ecc:copySKIP,pc=0x0809a74c:mixer"
PA="$PA,memwpc=0x08016eb4,memwpccap=200"
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" \
  -plugin "$PLUG,$PA" >$OUT/qmus.out 2>$OUT/qmus.err &
echo "launched pid $!  (boot 48s)"
sleep 48
$IR wake e 3000 >/dev/null 2>&1
echo "--- lang select ---"; $IR press ret 250 -u -w e --region right -r 8 -t 3000 -s 3 --name Pa 2>&1 | tail -1
echo "--- scroll to shuffle ---"; $IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 Pb 2>&1 | tail -1
echo "--- select play x5 ---"
for t in 1 2 3 4 5; do
  $IR seq e:120 ret:220 wait:1 >/dev/null 2>&1
  if grep -q StartPlayback $LOG 2>/dev/null; then echo "StartPlayback FIRED after try $t"; break; fi
done
sleep 5
echo "=========== READS ==========="
echo "--- codec type region (state+0x58 = 0x22010734) ---"
$IR raw 'xp/8xw 0x22010720' 2>&1 | grep -E '0x2201'
echo "--- audio state head 0x220106dc (+0x47 gate at 0x22010723, +0x58 type at 0x22010734) ---"
$IR raw 'xp/24xw 0x220106dc' 2>&1 | grep -E '0x2201'
echo "--- exchange 6 slot 0x0897fb34 (0=signalled, 0x20=t20 waiting, 0xffffffff=unalloc) ---"
$IR raw 'xp/2xw 0x0897fb30' 2>&1 | grep -E '0x0897'
echo "--- mbox 0x52 @0x0897827c ---"
$IR raw 'xp/8xw 0x08978278' 2>&1 | grep -E '0x0897'
echo "=========== MARKERS ==========="
grep -aoE '(StartPlayback|engine|copyPCM|copy32REAL|copySKIP|mixer)' $LOG | sort | uniq -c
echo "--- copy32 REAL destination stores (memwpc 0x08016eb4) ---"
grep -a 'memwpc\|MEMWPC\|0x08016eb4' $LOG | head -20
echo "=========== WAV ==========="
python3 tools/ipod_classic/wavcheck.py /home/dmontero/6g/probe1.wav 2>&1 | tail -8
echo "=========== resets=$(grep -c 'software reset' $OUT/qmus.err) ==========="
pkill -9 -x qmus 2>/dev/null
echo DONE
