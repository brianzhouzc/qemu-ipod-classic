#!/bin/bash
# Attach gdb, break in AAC decode (freezes guest -> defeats the stall watchdog),
# then pmemsave SDRAM via the HMP monitor for offline AAC needle-search.
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
PA="nodefault=on,always=on,cap=6,window=8000,out=$LOG"
PA="$PA,pc=0x08150d84:StartPlayback,pc=0x081e9e70:innerdec,pc=0x081e93d8:rpull,pc=0x0808170c:engine"
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234 \
  -plugin "$PLUG,$PA" >$OUT/qmem.out 2>$ERR &
QPID=$!; echo "launched qemu pid $QPID"
# Attach gdb early (during boot) so the breakpoint is set before the single decode.
sleep 6
setsid gdb-multiarch -q -nx -batch -x /home/dmontero/dbg/gdbdump.py >$OUT/gdb.out 2>$OUT/gdb.err &
GPID=$!; echo "launched gdb pid $GPID"
# wait for gdb to arm
for i in $(seq 1 30); do [ -f /home/dmontero/6g/GDBREADY ] && break; sleep 1; done
echo "gdb armed=$([ -f /home/dmontero/6g/GDBREADY ] && echo yes || echo no)"
# navigate to Now Playing
sleep 46
$IR wake e >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name L1 >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:2 L2 >/dev/null 2>&1
for i in 1 2 3 4 5 6 7 8; do
  $IR seq e:120 wait:1 ret:250 wait:2 z >/dev/null 2>&1
  [ -f /home/dmontero/6g/HALTED ] && { echo "HALTED after play try $i"; break; }
  grep -aqE 'cpu[0-9]  (innerdec|rpull|engine|StartPlayback) ' $LOG 2>/dev/null && echo "playback signs try $i"
done
# wait for the breakpoint halt
for i in $(seq 1 40); do [ -f /home/dmontero/6g/HALTED ] && break; sleep 1; done
echo "halted=$([ -f /home/dmontero/6g/HALTED ] && echo yes || echo no)"
echo "=== pmemsave SDRAM 0x08000000 +64MB (guest frozen at bp) ==="
exec 3<>/dev/tcp/127.0.0.1/4455
echo "pmemsave 0x08000000 0x04000000 $DUMP" >&3
sleep 10
exec 3<&-
sleep 1
ls -la $DUMP 2>&1
echo "=== gdb halt pc ==="; grep -aE 'HALTED|pc ' $OUT/gdb.out | tail -4
echo "=== pctrace hits ==="
sed -n '/PCTRACE summary/,$p' $LOG | grep -aE 'StartPlayback|innerdec|rpull|engine'
echo "resets=$(grep -c 'software reset' $ERR)"
pkill -9 gdb-multiarch 2>/dev/null; pkill -9 -x qmus 2>/dev/null
echo DONE
