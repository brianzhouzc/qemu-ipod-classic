#!/bin/bash
# Assault #13: trace the source<->copy BINDING seam that prior sessions saw the
# symptom of (descriptor 0x0bb12d54 uninitialised, [descr+5]=0x5d) but never
# traced at the binding function itself.
#
# Decoded from iram.dis:
#   0x22005e58  bindchk : if [descr+5]==1 return; else bl job_setup 0x22006078
#   0x22006078  jobsetup: create/return object 0x220106dc (motor render ctx)
#   0x220060e0  jobenq  : [0x22008c1c]++ (job count) + signal motor 0x220106d0
#   0x22006118  dpump   : the render pump (already known)
#   0x08016eb4  copy32  : the invariant (first native PCM sample)
# memw on 0x22008c1c catches every increment of the job count (str at 220060f8).
#
# Question: are bindchk / jobenq ever reached in cold start, and does the job
# count ever go nonzero? If bindchk fires but the bind virtual-call bails, the
# seam is inside the bind; if bindchk never fires, the seam is upstream.
set +e
TAG=${1:-bind}
ROOT=$HOME/qemu-ios
OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
IMG=$HOME/6g/Emulti.img
OUT=$HOME/6g/remote; mkdir -p "$OUT"
export IREMOTE_OUT=$OUT
cd "$ROOT" || exit 1
IR="python3 tools/ipod_classic/iremote.py"
LOG=$HOME/6g/meas_$TAG.log; rm -f "$LOG"
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
BIN=$ROOT/build/qmus_$TAG
pkill -9 -f "qmus_$TAG" 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$BIN"
export AUDIOPACE=1
PA="nodefault=on,always=on,cap=8,window=4000,out=$LOG"
PA="$PA,pc=0x081fa9e4:Shuffle,pc=0x08150d84:StartPlayback"
PA="$PA,pc=0x22005e58:bindchk,pc=0x22006078:jobsetup,pc=0x220060e0:jobenq"
PA="$PA,pc=0x22006118:dpump,pc=0x08016eb4:copy32,pc=0x0808170c:engine"
PA="$PA,memw=0x22008c1c-0x22008c20"
setsid "$BIN" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=$IMG,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234 \
  -plugin "$PLUG,$PA" >"$OUT/qmus_$TAG.out" 2>"$OUT/qmus_$TAG.err" &
echo "launched $TAG pid $!"
sleep 52
$IR wake e >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name L1 >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:2 L2 >/dev/null 2>&1
for i in 1 2 3 4 5 6 7 8; do
  $IR seq e:120 wait:1 ret:250 wait:2 z >/dev/null 2>&1
  if grep -aqE 'cpu[0-9]  (engine|bindchk|jobenq) ' "$LOG" 2>/dev/null; then echo "PROGRESS try $i"; break; fi
done
echo "=== 10s free-run ==="; sleep 10
echo "=== [$TAG] job-count word writes (MEMW 0x22008c1c; writer pc=0x220060f8=jobenq) ==="
grep -aE 'MEMW ' "$LOG" | head -20
echo "=== [$TAG] first hits of each label ==="
grep -aE 'cpu[0-9]  (Shuffle|StartPlayback|bindchk|jobsetup|jobenq|dpump|copy32|engine) ' "$LOG" | head -30
exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 2 && exec 3<&-
sleep 2
echo "=== [$TAG] PCTRACE summary ==="
sed -n '/PCTRACE summary/,$p' "$LOG" | head -30
echo "resets=$(grep -c 'software reset' "$OUT/qmus_$TAG.err")"
pkill -9 -f "qmus_$TAG" 2>/dev/null
echo "DONE $TAG"
