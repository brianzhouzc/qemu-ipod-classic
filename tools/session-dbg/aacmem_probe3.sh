#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
export IREMOTE_OUT=$OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/aacmem.log; rm -f $LOG
ERR=$OUT/qmem.err; rm -f $ERR
DUMP=/home/dmontero/6g/dram.bin; rm -f $DUMP
rm -f /home/dmontero/6g/GDBREADY /home/dmontero/6g/HALTED
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmus 2>/dev/null; pkill -9 gdb-multiarch 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1
PA="nodefault=on,always=on,cap=6,window=8000,out=$LOG,pc=0x08150d84:StartPlayback,pc=0x081e93d8:rpull"
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234 \
  -plugin "$PLUG,$PA" >$OUT/qmem.out 2>$ERR &
echo "launched qemu pid $!"
sleep 6
setsid gdb-multiarch -q -nx -batch -x /home/dmontero/dbg/gdbdump.py >$OUT/gdb.out 2>$OUT/gdb.err &
echo "launched gdb pid $!"
for i in $(seq 1 30); do [ -f /home/dmontero/6g/GDBREADY ] && break; sleep 1; done
echo "gdb armed=$([ -f /home/dmontero/6g/GDBREADY ] && echo yes || echo no)"
sleep 46
$IR wake e >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name L1 >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:2 L2 >/dev/null 2>&1
for i in 1 2 3 4 5 6 7 8; do
  $IR seq e:120 wait:1 ret:250 wait:2 z >/dev/null 2>&1
  grep -aq 'HALTED' /home/dmontero/6g/remote/gdb.out 2>/dev/null && { echo "gdb HALTED after play try $i"; break; }
done
echo "=== waiting for gdb dump to finish (guest frozen, no watchdog) ==="
for i in $(seq 1 120); do [ -f /home/dmontero/6g/HALTED ] && break; sleep 2; done
echo "dump_done=$([ -f /home/dmontero/6g/HALTED ] && echo yes || echo no)"
ls -la $DUMP 2>&1
echo "=== gdb.out ==="; tail -8 /home/dmontero/6g/remote/gdb.out
echo "resets=$(grep -c 'software reset' $ERR)"
pkill -9 gdb-multiarch 2>/dev/null; pkill -9 -x qmus 2>/dev/null
echo DONE
