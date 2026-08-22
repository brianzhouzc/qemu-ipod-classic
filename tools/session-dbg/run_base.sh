#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
export IREMOTE_OUT=$OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/base.log; rm -f $LOG
WAV=/home/dmontero/6g/base.wav; rm -f $WAV
ERR=$OUT/qbase.err; rm -f $ERR
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qbase 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qbase"
export AUDIOPACE=1 AUDIOWAV=$WAV
PA="nodefault=on,always=on,cap=6,window=4000,out=$LOG"
PA="$PA,pc=0x08150d84:StartPlayback,pc=0x0808170c:engine,pc=0x22005e48:srcpoll,pc=0x08016eb4:copy32,pc=0x08016e44:copyfn,pc=0x08016ecc:copyskip,pc=0x081e9e70:innerdec"
setsid "$ROOT/build/qbase" -M iPod-Classic -kernel "$OSOS" -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234 -plugin "$PLUG,$PA" >$OUT/qbase.out 2>$ERR &
echo "launched pid $!"
sleep 52
$IR wake e >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name L1 >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:2 L2 >/dev/null 2>&1
CONF=0
for i in 1 2 3 4 5 6 7 8; do
  $IR seq e:120 wait:1 ret:250 wait:2 z >/dev/null 2>&1
  if grep -aqE 'cpu[0-9]  (engine|srcpoll|copyfn) ' $LOG 2>/dev/null; then echo "PROGRESS try $i"; CONF=1; break; fi
done
echo "CONF=$CONF"
echo '=== free-run 25s ==='; sleep 25
exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 2 && exec 3<&-
sleep 2
echo '=== counters ==='; grep -aE 'cpu[0-9]  (StartPlayback|engine|srcpoll|copy32|copyfn|copyskip|innerdec) ' $LOG | tail -20
echo "resets=$(grep -ac 'software reset' $ERR)"
python3 - "$WAV" << 'PYEOF'
import sys,wave,array
try:
    w=wave.open(sys.argv[1],'rb'); n=w.getnframes(); a=array.array('h'); a.frombytes(w.readframes(n))
    nz=sum(1 for x in a if x); pk=max((abs(x) for x in a), default=0)
    print('WAV frames=%d nonzero=%.2f%% peak=%d'%(n,100.0*nz/max(1,len(a)),pk))
except Exception as e: print('wav err',e)
PYEOF
pkill -9 -x qbase 2>/dev/null; echo DONE
