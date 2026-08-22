#!/bin/bash
# Locate the compressed AAC that StreamCache loaded into guest DRAM.
# Clean run (NO shim, default inert). Navigate to Now Playing (Shuffle Songs),
# confirm StartPlayback + decode path, then pmemsave the whole SDRAM to a host
# file for offline needle-search against the on-disk mdat bytes.
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
export IREMOTE_OUT=$OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/aacmem.log; rm -f $LOG
ERR=$OUT/qmem.err; rm -f $ERR
DUMP=/home/dmontero/6g/dram.bin; rm -f $DUMP
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1
PA="nodefault=on,always=on,cap=6,window=8000,out=$LOG"
PA="$PA,pc=0x08150d84:StartPlayback"
PA="$PA,pc=0x081c0a1c:decmeth,pc=0x081c1508:fpull,pc=0x081e93d8:rpull,pc=0x081e9e70:innerdec"
PA="$PA,pc=0x08123dcc:disp,pc=0x08123970:h12,pc=0x0808170c:engine"
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234 \
  -plugin "$PLUG,$PA" >$OUT/qmem.out 2>$ERR &
echo "launched pid $!"
sleep 52
$IR wake e >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name L1 >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:2 L2 >/dev/null 2>&1
CONF=0
for i in 1 2 3 4 5 6 7 8; do
  $IR seq e:120 wait:1 ret:250 wait:2 z >/dev/null 2>&1
  if grep -aqE 'cpu[0-9]  (h12|disp|decmeth|StartPlayback|engine) ' $LOG 2>/dev/null; then echo "PROGRESS try $i"; CONF=1; break; fi
done
echo "CONF=$CONF"
echo "=== let StreamCache prefetch settle ${SETTLE:-15}s ==="; sleep "${SETTLE:-15}"
echo "=== pmemsave SDRAM 0x08000000 +64MB -> $DUMP ==="
exec 3<>/dev/tcp/127.0.0.1/4455
echo "pmemsave 0x08000000 0x04000000 $DUMP" >&3
sleep 8
echo "info registers" >&3
sleep 1
exec 3<&-
sleep 2
ls -la $DUMP
echo "=== pctrace hits ==="
sed -n '/PCTRACE summary/,$p' $LOG | grep -aE 'StartPlayback|decmeth|fpull|rpull|innerdec|disp|h12|engine'
echo "resets=$(grep -c 'software reset' $ERR)"
# leave qemu running for optional gdb probe unless KILL set
[ -n "$KILL" ] && pkill -9 -x qmus 2>/dev/null
echo DONE
