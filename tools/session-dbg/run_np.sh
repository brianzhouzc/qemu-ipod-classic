#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT; export IREMOTE_OUT=$OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/np.log; rm -f $LOG /home/dmontero/6g/np.wav
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1
export HLE_NP="${HLE_NP:-1}" HLE_NP_TRACE="${HLE_NP_TRACE:-1}" HLE_NP_DEC_ONLY="${HLE_NP_DEC_ONLY:-0}"
export HLE_AUDIO_SUSTAIN=1
export AUDIOWAV=/home/dmontero/6g/np.wav
PA="nodefault=on,always=on,cap=6,window=4000,out=$LOG"
PA="$PA,pc=0x08150d84:StartPlayback,pc=0x08123970:h12,pc=0x0808170c:engine,pc=0x08016eb4:copy32,pc=0x081e9e70:innerdec,pc=0x0809a74c:mixer"
PA="$PA,sendex=0x22004138:3:0x6:ex6"
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" \
  -plugin "$PLUG,$PA" >$OUT/qmus_np.out 2>$OUT/qmus_np.err &
echo "launched pid $! (HLE_NP=$HLE_NP dec_only=$HLE_NP_DEC_ONLY)"
sleep 52
$IR wake e >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name N1 >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:2 N2 >/dev/null 2>&1
# retry play until real decode confirmed (ex6 SEND or innerdec)
for i in 1 2 3 4 5 6 7 8 9 10; do
  $IR seq e:120 wait:1 ret:250 wait:2 z >/dev/null 2>&1
  if grep -aqE "cpu[0-9]  (ex6|innerdec) " $LOG 2>/dev/null; then echo "regime-A confirmed try $i"; break; fi
done
echo "playing 25s..."; sleep 25
echo "=== HLE_NP shim trace (head) ==="; grep -aE "HLE_NP" $OUT/qmus_np.err | grep -vE "attempt" | head -14
echo "=== HLE_NP attempts (tail) ==="; grep -aE "HLE_NP attempt" $OUT/qmus_np.err | tail -3
echo "=== counters ==="; grep -aE "cpu[0-9]  (StartPlayback|h12|engine|copy32|innerdec|mixer|ex6) " $LOG | tail -16
echo "=== resets ==="; grep -c "software reset" $OUT/qmus_np.err
echo "=== WAV analysis ==="
python3 - <<PY
import numpy as np,os
p="/home/dmontero/6g/np.wav"
if not os.path.exists(p): print("no wav"); raise SystemExit
f=open(p,"rb"); f.seek(44); a=np.frombuffer(f.read(),dtype=np.int16)
if len(a)<4: print("empty"); raise SystemExit
a=a[:len(a)//2*2].reshape(-1,2).astype(float); L,R=a[:,0],a[:,1]
nz=np.count_nonzero(a)/a.size*100
print(f"frames={len(a)} dur={len(a)/44100:.1f}s RMS={np.sqrt(np.mean(a**2)):.1f} max={int(np.max(np.abs(a)))} nonzero={nz:.1f}% L==R={np.mean(L==R)*100:.0f}% rms(L-R)={np.sqrt(np.mean((L-R)**2)):.1f}")
PY
exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 1 && exec 3<&-
pkill -9 -x qmus 2>/dev/null; echo DONE
