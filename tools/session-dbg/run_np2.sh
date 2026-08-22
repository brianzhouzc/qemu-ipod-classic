#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT; export IREMOTE_OUT=$OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/np2.log; rm -f $LOG /home/dmontero/6g/np2.wav
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1 HLE_AUDIO_SUSTAIN=1
export HLE_AUDIO_DATAPATH=1 HLE_AUDIO_DISK=/home/dmontero/6g/Emulti.img
export HLE_NP=1 HLE_NP_TRACE=1 HLE_NP_NOFILL=1
export AUDIOWAV=/home/dmontero/6g/np2.wav
PA="nodefault=on,always=on,cap=6,window=4000,regcap=8,out=$LOG"
PA="$PA,pc=0x08150d84:StartPlayback,pc=0x0808170c:engine,pc=0x08016eb4:copy32,pc=0x0809a74c:mixer,pc=0x22002fac:t25render,pc=0x220073c8:r73c8,pc=0x22007440:r7440,pc=0x08016ecc:copySKIP,regpc=0x08016e44:COPYENT,sendex=0x22004138:3:0x6:ex6"
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" \
  -plugin "$PLUG,$PA" >$OUT/qmus_np2.out 2>$OUT/qmus_np2.err &
echo "launched pid $!"
sleep 52
$IR wake e >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name Q1 >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:2 Q2 >/dev/null 2>&1
for i in $(seq 1 12); do
  $IR seq e:120 wait:1 ret:250 wait:2 z >/dev/null 2>&1
  if grep -aqE "cpu[0-9]  (ex6|copy32) " $LOG 2>/dev/null; then echo "play confirmed try $i"; break; fi
done
echo "playing 25s..."; sleep 25
echo "=== HLE trace (NP + AUDIO decode) ==="; grep -aE "HLE_NP:|HLE_AUDIO:" $OUT/qmus_np2.err | grep -vE "attempt" | head -12
echo "=== counters (copy32 is the goal) ==="; grep -aE "cpu[0-9]  (StartPlayback|engine|copy32|mixer|copySKIP|ex6) " $LOG | tail -12
echo "=== resets ==="; grep -c "software reset" $OUT/qmus_np2.err
echo "=== WAV ==="
python3 - <<PY
import numpy as np,os
p="/home/dmontero/6g/np2.wav"
if not os.path.exists(p): print("no wav"); raise SystemExit
f=open(p,"rb"); f.seek(44); a=np.frombuffer(f.read(),dtype=np.int16)
if len(a)<4: print("empty"); raise SystemExit
a=a[:len(a)//2*2].reshape(-1,2).astype(float); L,R=a[:,0],a[:,1]
print(f"frames={len(a)} dur={len(a)/44100:.1f}s RMS={np.sqrt(np.mean(a**2)):.1f} max={int(np.max(np.abs(a)))} nonzero={np.count_nonzero(a)/a.size*100:.1f}% L==R={np.mean(L==R)*100:.0f}% rms(L-R)={np.sqrt(np.mean((L-R)**2)):.1f}")
PY
exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 1 && exec 3<&-
pkill -9 -x qmus 2>/dev/null; echo DONE
