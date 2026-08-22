#!/bin/bash
# Session-9: range-trace the codec region during setup + watchdog bursts.
# Answers: does the AAC decode math EVER run (regime A)?
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
export IREMOTE_OUT=$OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/trace.log; rm -f $LOG
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1
PA="nodefault=on,always=on,cap=6,window=8000,out=$LOG"
PA="$PA,pc=0x08150d84:StartPlayback,pc=0x08123970:h12,pc=0x08123bfc:dwork,pc=0x08016eb4:copy32,pc=0x0809a74c:mixer,pc=0x0808170c:engine"
# triggers open the range window; range = codec math region (0x0808-0x080b)
PA="$PA,trig=0x08150d84,trig=0x08123970,trig=0x08123284,trig=0x08123bfc"
PA="$PA,range=0x08080000-0x080b0000"
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234 \
  -plugin "$PLUG,$PA" >$OUT/qmus.out 2>$OUT/qmus.err &
echo "launched pid $!"
sleep 52
$IR wake e >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name L1 >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:2 L2 >/dev/null 2>&1
for i in 1 2 3 4 5 6 7 8; do
  $IR seq e:120 wait:1 ret:250 wait:2 z >/dev/null 2>&1
  if grep -aqE 'cpu[0-9]  (h12|dwork) ' $LOG 2>/dev/null; then echo "PROGRESS try $i"; break; fi
done
echo "=== 22s free-run to catch >=1 watchdog burst (15s) ==="; sleep 22
echo "=== counter hits ==="
grep -aE 'cpu[0-9]  (StartPlayback|h12|dwork|copy32|mixer|engine) ' $LOG | head -20
echo "=== RANGE-captured codec blocks (unique PCs, region 0808-080b) ==="
grep -aE 'RANGE' $LOG | grep -aoE '@0x080[0-9a-f]{5}' | sort | uniq -c | sort -rn | head -90
echo "=== RANGE block count ==="
grep -acE 'RANGE' $LOG
echo "=== distinct RANGE PCs ==="
grep -aE 'RANGE' $LOG | grep -aoE '0x080[0-9a-f]{5}' | sort -u | wc -l
pkill -9 -x qmus 2>/dev/null
echo DONE
