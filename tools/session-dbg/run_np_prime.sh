#!/bin/bash
# Assault #13 (full priming HLE): run the existing HLE_NP platform with the
# host-decode datapath feeding it, trace every kick, and watch copy32. Measures
# whether the priming lands or defers-forever (the injection-window wall of
# wiki/19 §11.22-D), and if it lands, whether copy32 moves off 0.
set +e
TAG=${1:-np}
shift
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

# host-decode datapath (feeds have_pcm) + priming platform + trace
export AUDIOPACE=1 AUDIOLIVE=1
export HLE_AUDIO_DATAPATH=1 HLE_AUDIO_DISK=$IMG HLE_AUDIO_SUSTAIN=1
export HLE_NP=1 HLE_NP_TRACE=1
# extra knobs passed as KEY=VAL args
for kv in "$@"; do export "$kv"; done

PA="nodefault=on,always=on,cap=6,window=6000,out=$LOG"
PA="$PA,pc=0x081fa9e4:Shuffle,pc=0x08150d84:StartPlayback"
PA="$PA,pc=0x08016e44:copyent,pc=0x08016e7c:r4chk,pc=0x08016e88:gatechk,pc=0x08016e94:copygo,pc=0x08016eb4:copy32,pc=0x08123970:h12,pc=0x0808170c:engine"
PA="$PA,regpc=0x08016e44:copyent_r,regpc=0x08016e88:gate_r7,regcap=20"
PA="$PA,pc=0x22006078:jobsetup,pc=0x220060e0:jobenq"
PA="$PA,memw=0x22008c1c-0x22008c20"

setsid "$BIN" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=$IMG,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234 \
  -audio none \
  -plugin "$PLUG,$PA" >"$OUT/qmus_$TAG.out" 2>"$OUT/qmus_$TAG.err" &
echo "launched $TAG pid $!"
sleep 52
$IR wake e >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name L1 >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:2 L2 >/dev/null 2>&1
for i in 1 2 3 4 5 6 7 8; do
  $IR seq e:120 wait:1 ret:250 wait:2 z >/dev/null 2>&1
  if grep -aqE 'cpu[0-9]  (engine|copy32) ' "$LOG" 2>/dev/null; then echo "PROGRESS try $i"; break; fi
done
echo "=== 12s free-run (let priming timer fire) ==="; sleep 12
echo "=== [$TAG] HLE_NP trace (kicks / deferred) ==="
grep -aE 'HLE_NP' "$OUT/qmus_$TAG.err" | head -40
echo "=== [$TAG] deferred/landed tally ==="
echo "deferred=$(grep -ac 'deferred (cpu busy)' "$OUT/qmus_$TAG.err")  DECODEkicks=$(grep -ac 'HLE_NP: DECODE #' "$OUT/qmus_$TAG.err")  bindkicks=$(grep -acE 'HLE_NP: #[0-9]' "$OUT/qmus_$TAG.err")"
echo "=== [$TAG] counters ==="
grep -aE 'cpu[0-9]  (Shuffle|StartPlayback|copy32|h12|engine|jobsetup|jobenq) |MEMW ' "$LOG" | head -30
exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 2 && exec 3<&-
sleep 2
echo "=== [$TAG] PCTRACE summary ==="
sed -n '/PCTRACE summary/,$p' "$LOG" | head -22
echo "resets=$(grep -c 'software reset' "$OUT/qmus_$TAG.err")"
pkill -9 -f "qmus_$TAG" 2>/dev/null
echo "DONE $TAG"
