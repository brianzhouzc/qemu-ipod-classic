#!/bin/bash
# Navigate to Now Playing WITHOUT gdb, let the pipeline reach the stable deadlock,
# THEN attach gdb (halts on connect) and dump SDRAM. Catches whatever StreamCache
# loaded by the stable-deadlock point.
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
export IREMOTE_OUT=$OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/aacmem.log; rm -f $LOG
ERR=$OUT/qmem.err; rm -f $ERR
rm -f /home/dmontero/6g/dram2.bin /home/dmontero/6g/HALTED2
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmus 2>/dev/null; pkill -9 gdb-multiarch 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1
PA="nodefault=on,always=on,cap=6,window=8000,out=$LOG,pc=0x08150d84:StartPlayback,pc=0x081e93d8:rpull,pc=0x0808170c:engine"
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234 \
  -plugin "$PLUG,$PA" >$OUT/qmem.out 2>$ERR &
echo "launched qemu pid $!"
sleep 52
$IR wake e >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name L1 >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:2 L2 >/dev/null 2>&1
SP=0
for i in 1 2 3 4 5 6 7 8; do
  $IR seq e:120 wait:1 ret:250 wait:2 z >/dev/null 2>&1
  if grep -aqE 'cpu[0-9]  (StartPlayback|rpull|engine) ' $LOG 2>/dev/null; then echo "StartPlayback try $i"; SP=1; break; fi
done
echo "SP=$SP"
echo "=== let pipeline settle into stable deadlock ${DL:-5}s then attach gdb ==="
sleep "${DL:-5}"
setsid gdb-multiarch -q -nx -batch -x /home/dmontero/dbg/gdbdump2.py >$OUT/gdb2.out 2>$OUT/gdb2.err &
echo "gdb attach pid $!"
for i in $(seq 1 60); do [ -f /home/dmontero/6g/HALTED2 ] && break; sleep 1; done
echo "dump2_done=$([ -f /home/dmontero/6g/HALTED2 ] && echo yes || echo no)"
ls -la /home/dmontero/6g/dram2.bin 2>&1
echo "=== gdb2.out ==="; cat $OUT/gdb2.out
echo "=== pctrace hits ==="; sed -n '/PCTRACE summary/,$p' $LOG | grep -aE 'StartPlayback|rpull|engine'
echo "resets=$(grep -c 'software reset' $ERR)"
pkill -9 gdb-multiarch 2>/dev/null; pkill -9 -x qmus 2>/dev/null
echo DONE
