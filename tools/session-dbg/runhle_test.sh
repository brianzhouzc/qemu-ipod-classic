#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/hletest.log; rm -f $LOG
WAV=/home/dmontero/6g/hle_out.wav; rm -f $WAV
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1
export HLE_AUDIO_KICK=1
export HLE_AUDIO_KICK_TRACE=1
export AUDIOWAV=$WAV
PA="nodefault=on,always=on,cap=3,out=$LOG,pc=0x08150d84:StartPlayback"
PA="$PA,pc=0x08123dcc:disp,pc=0x08123970:h12,pc=0x08123bfc:dwork"
PA="$PA,pc=0x0809a74c:mixer,pc=0x08016e44:copyfn,pc=0x08016eb4:copy32"
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
exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 2 && exec 3<&-
sleep 2
echo "=== HLE stderr ==="; grep -a "HLE_AUDIO_KICK" $OUT/qmus.err | head -20
echo "=== first-nonzero PCM announce ==="; grep -a "FIRST NON-ZERO" $OUT/qmus.err | head
echo "=== AUDIOWAV summary ==="; grep -a "AUDIOWAV" $OUT/qmus.err | tail -3
echo "=== pctrace counters ==="; sed -n "/PCTRACE summary/,\$p" $LOG | head -30
echo "=== wav size ==="; ls -la $WAV 2>/dev/null
echo "=== nonzero PCM frames in wav (python) ==="
python3 - "$WAV" << "PYEOF"
import sys,wave,struct
try:
    w=wave.open(sys.argv[1],"rb"); n=w.getnframes(); ch=w.getnchannels(); sw=w.getsampwidth()
    d=w.readframes(n); import array; a=array.array("h"); a.frombytes(d)
    nz=sum(1 for x in a if x!=0); tot=len(a)
    # L vs R difference (stereo real?)
    L=a[0::2]; R=a[1::2]; diff=sum(1 for i in range(min(len(L),len(R))) if L[i]!=R[i])
    print("frames=%d ch=%d sw=%d samples=%d nonzero=%d LneR=%d"%(n,ch,sw,tot,nz,diff))
except Exception as e:
    print("wav read err:",e)
PYEOF
echo "resets=$(grep -c "software reset" $OUT/qmus.err)"
pkill -9 -x qmus 2>/dev/null
echo DONE
