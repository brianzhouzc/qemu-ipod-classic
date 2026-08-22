#!/bin/bash
# HLE_DUAL_KICK: kick the DECODER (produce frame 0) AND the MOTOR (consume it),
# decoder-first, to break the symmetric bootstrap deadlock. Measure ordered:
# (a) decode math runs, (b) PCM into mixer source 0x08888xxx (non-memset),
# (c) copy32>0, (d) sustain. Reliable navigation (run_rk.sh pattern).
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
export IREMOTE_OUT=$OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/dk.log; rm -f $LOG
WAV=/home/dmontero/6g/dk_out.wav; rm -f $WAV
ERR=$OUT/qmus_dk.err; rm -f $ERR
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1
export HLE_DUAL_KICK="${DK:-1}" HLE_DUAL_KICK_TRACE=1
[ -n "$DECONLY" ] && export HLE_DUAL_KICK_DEC_ONLY=1
[ -n "$MOTORONLY" ] && export HLE_DUAL_KICK_MOTOR_ONLY=1
[ -n "$DECMAX" ] && export HLE_DUAL_KICK_DEC_MAX=$DECMAX
export AUDIOWAV=$WAV
PA="nodefault=on,always=on,cap=6,window=4000,out=$LOG"
PA="$PA,pc=0x08150d84:StartPlayback"
PA="$PA,pc=0x081c0a1c:decmeth,pc=0x081c1508:fpull,pc=0x081e93d8:rpull,pc=0x081e9e70:innerdec"
PA="$PA,pc=0x08016eb4:copy32,pc=0x0809a74c:mixer,pc=0x0808170c:engine,pc=0x08123dcc:disp,pc=0x08123970:h12,pc=0x22006574:codecsetup,pc=0x22006118:dpump,pc=0x22005e48:srcpoll,pc=0x22006308:dpgateclr"
PA="$PA,memw=0x08888000-0x0888c000"
PA="$PA,memwpc=0x08016eb4:copydst"
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS"   -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot   -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234   -plugin "$PLUG,$PA" >$OUT/qmus_dk.out 2>$ERR &
echo "launched pid $! (DK=$HLE_DUAL_KICK DECONLY=${DECONLY:-0} MOTORONLY=${MOTORONLY:-0} DECMAX=${DECMAX:-0})"
sleep 52
$IR wake e >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name L1 >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:2 L2 >/dev/null 2>&1
CONF=0
for i in 1 2 3 4 5 6 7 8; do
  $IR seq e:120 wait:1 ret:250 wait:2 z >/dev/null 2>&1
  if grep -aqE 'cpu[0-9]  (h12|disp|decmeth) ' $LOG 2>/dev/null; then echo "PROGRESS try $i"; CONF=1; break; fi
done
echo "CONF=$CONF"
echo "=== free-run ${FREE:-20}s ==="; sleep "${FREE:-20}"
exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 2 && exec 3<&-
sleep 2
echo "=== HLE dual-kick trace (first + last) ==="
grep -a 'HLE_DUAL_KICK' $ERR | grep -aE 'armed|play start' | head -4
grep -a 'HLE_DUAL_KICK:' $ERR | grep -aE 'DECODE frame|ring MOTOR' | head -3
grep -a 'HLE_DUAL_KICK:' $ERR | grep -aE 'DECODE frame|ring MOTOR' | tail -3
echo "dec_kicks_total=$(grep -ac 'DECODE frame' $ERR)  motor_kicks_total=$(grep -ac 'ring MOTOR' $ERR)"
grep -a 'HLE_DUAL_KICK attempt' $ERR | tail -2
echo "=== PCTRACE summary counters ==="
sed -n '/PCTRACE summary/,$p' $LOG | grep -aE 'codecsetup|dpump|srcpoll|dpgateclr|decmeth|fpull|rpull|innerdec|copy32|mixer|engine|disp|h12|StartPlayback' | head -20
echo "=== mixer-source 0x08888xxx writers (NON-memset = decoder PCM) ==="
sed -n '/mem-write watch/,$p' $LOG | grep -aE 'writer pc=' | grep -avE '0x22000198|0x220001a0'
echo "   (memset writers, for reference:)"; sed -n '/mem-write watch/,$p' $LOG | grep -aE 'writer pc=(0x22000198|0x220001a0)'
echo "=== copy32 destinations (memwpc) ==="; sed -n '/mem-write-at-pc/,$p' $LOG | grep -aE 'copydst' | head
grep -aE 'cpu[0-9]  MEMWPC copydst' $LOG | head -4
echo "=== WAV nonzero/stereo (real audio = high nonzero + L!=R) ==="
python3 - "$WAV" << 'PYEOF'
import sys,wave,array
try:
    w=wave.open(sys.argv[1],'rb'); n=w.getnframes(); a=array.array('h'); a.frombytes(w.readframes(n))
    L=a[0::2]; R=a[1::2]; nz=sum(1 for x in a if x)
    diff=sum(1 for i in range(min(len(L),len(R))) if L[i]!=R[i])
    pk=max((abs(x) for x in a), default=0)
    print('frames=%d nonzero=%d(%.2f%%) LneR=%d peak=%d'%(n,nz,100.0*nz/max(1,len(a)),diff,pk))
except Exception as e: print('wav err',e)
PYEOF
echo "resets=$(grep -c 'software reset' $ERR)"
pkill -9 -x qmus 2>/dev/null
echo DONE
