#!/bin/bash
# Session-11: HLE_RENDER_KICK -- ring the render doorbell to engine t25 and
# measure whether the guest engine renders REAL PCM (copy32>0 + non-memset
# writers into the mixer PCM source). Reliable navigation (runmeas.sh pattern).
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
export IREMOTE_OUT=$OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/rk.log; rm -f $LOG
WAV=/home/dmontero/6g/rk_out.wav; rm -f $WAV
ERR=$OUT/qmus.err
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1
export HLE_RENDER_KICK="${RK:-1}" HLE_RENDER_KICK_TRACE=1
[ -n "$RKONCE" ] && export HLE_RENDER_KICK_ONCE=1
export AUDIOWAV=$WAV
PA="nodefault=on,always=on,cap=6,window=4000,out=$LOG"
PA="$PA,pc=0x08150d84:StartPlayback"
PA="$PA,pc=0x08123dcc:disp,pc=0x08123970:h12,pc=0x08123bfc:dwork,pc=0x081239bc:gateconsume,pc=0x08016eb4:copy32"
PA="$PA,pc=0x0809a74c:mixer,pc=0x0808170c:engine"
PA="$PA,sendex=0x22004138:3:0x6:ex6"
PA="$PA,memw=0x08888000-0x0888c000"
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234 \
  -plugin "$PLUG,$PA" >$OUT/qmus.out 2>$ERR &
echo "launched pid $! (RK=$HLE_RENDER_KICK ONCE=${RKONCE:-0})"
sleep 52
$IR wake e >/dev/null 2>&1
# RELIABLE navigation: language -> main menu, retry until screen changes
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name L1 >/dev/null 2>&1
# scroll to the last main-menu item (Shuffle Songs)
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:2 L2 >/dev/null 2>&1
CONF=0
for i in 1 2 3 4 5 6 7 8; do
  $IR seq e:120 wait:1 ret:250 wait:2 z >/dev/null 2>&1
  if grep -aqE 'cpu[0-9]  (h12|ex6) ' $LOG 2>/dev/null; then echo "PROGRESS(decode-confirmed) try $i"; CONF=1; break; fi
done
echo "CONF=$CONF"
echo "=== free-run ${FREE:-18}s to let render+decode proceed ==="; sleep "${FREE:-18}"
exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 2 && exec 3<&-
sleep 2
echo "=== counters (cpu-tagged hit lines, live) ==="
grep -aE 'cpu[0-9]  (StartPlayback|disp|h12|dwork|gateconsume|copy32|mixer|engine|ex6) ' $LOG | head -30
echo "=== HLE doorbell trace ==="
grep -a "HLE_RENDER_KICK:" $ERR | grep -aE "rang render|armed|play start" | head -8
grep -a "rang render doorbell" $ERR | head -3
echo "rang_total=$(grep -ac 'rang render doorbell' $ERR)"
grep -a "HLE_RENDER_KICK attempt" $ERR | tail -3
echo "=== PCTRACE summary (counters + memw distinct writers) ==="
sed -n '/PCTRACE summary/,$p' $LOG | head -50
echo "=== non-memset writers into 0x08888xxx (real decode?) ==="
sed -n '/mem-write watch/,$p' $LOG | grep -aE 'writer pc=' | grep -avE '0x22000198|0x220001a0'
echo "=== WAV nonzero/stereo ==="
python3 - "$WAV" << 'PYEOF'
import sys,wave,array
try:
    w=wave.open(sys.argv[1],"rb"); n=w.getnframes(); a=array.array("h"); a.frombytes(w.readframes(n))
    L=a[0::2]; R=a[1::2]; nz=sum(1 for x in a if x)
    diff=sum(1 for i in range(min(len(L),len(R))) if L[i]!=R[i])
    pk=max((abs(x) for x in a), default=0)
    print("frames=%d nonzero=%d(%.2f%%) LneR=%d peak=%d"%(n,nz,100.0*nz/max(1,len(a)),diff,pk))
except Exception as e: print("wav err",e)
PYEOF
echo "resets=$(grep -c 'software reset' $ERR)"
pkill -9 -x qmus 2>/dev/null
echo DONE
