#!/bin/bash
# Assault #13 fase C: run the passive source prefill (HLE_NP_PASSIVE fills the
# copy source buffer) + the gdb copy-gate PC-hook (forces [r7+5]=1 at the exact
# instruction) and measure whether copy32 fires and whether it CASCADES.
set +e
TAG=${1:-cascade}
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
pkill -9 -f "qmus_$TAG" 2>/dev/null; pkill -9 -f "copygate_hook" 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$BIN"

export AUDIOPACE=1
export HLE_AUDIO_DATAPATH=1 HLE_AUDIO_DISK=$IMG HLE_AUDIO_SUSTAIN=1
export HLE_NP=1 HLE_NP_PASSIVE=1

PA="nodefault=on,always=on,cap=8,window=8000,out=$LOG"
PA="$PA,pc=0x08150d84:StartPlayback,pc=0x0808170c:engine"
PA="$PA,pc=0x08016e44:copyent,pc=0x08016e94:copygo,pc=0x08016eb4:copy32"
PA="$PA,pc=0x08123970:h12,pc=0x08123bfc:dwork"
PA="$PA,pc=0x08017378:credithdlr,pc=0x0808af58:pumppred,pc=0x22006324:pumploop"
PA="$PA,pc=0x22006304:rendret,pc=0x0809a74c:mixer,pc=0x080170cc:mixpost,pc=0x22006310:pumpretry"
PA="$PA,regpc=0x08016eb4:copydst,regpc=0x08016e94:copystart,regcap=12"

setsid "$BIN" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=$IMG,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234 \
  -plugin "$PLUG,$PA" >"$OUT/qmus_$TAG.out" 2>"$OUT/qmus_$TAG.err" &
echo "launched $TAG pid $!"

# Attach the gdb PC-hook in the background (breakpoint persists across boot).
( timeout 200 gdb-multiarch -q -batch -x tools/session-dbg/cascade_hook.gdb \
    >"$OUT/gdb_$TAG.out" 2>"$OUT/gdb_$TAG.err" ) &
GDBSH=$!
sleep 3
echo "gdb hook attached (pid $GDBSH); grep: $(grep -ac 'cascade hooks armed' "$OUT/gdb_$TAG.out")"

sleep 50
$IR wake e >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name L1 >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:2 L2 >/dev/null 2>&1
for i in 1 2 3 4 5 6 7 8; do
  $IR seq e:120 wait:1 ret:250 wait:2 z >/dev/null 2>&1
  if grep -aqE 'cpu[0-9]  (copy32|engine) ' "$LOG" 2>/dev/null; then echo "PROGRESS try $i"; break; fi
done
echo "=== 15s free-run (watch for cascade) ==="; sleep 15
echo "=== [$TAG] counters ==="
grep -aE 'cpu[0-9]  (StartPlayback|engine|copyent|copygo|copy32|h12|dwork|gateconsume) ' "$LOG" | head -30
exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 2 && exec 3<&-
sleep 2
echo "=== [$TAG] PCTRACE summary ==="
sed -n '/PCTRACE summary/,$p' "$LOG" | head -26
echo "resets=$(grep -c 'software reset' "$OUT/qmus_$TAG.err")"
pkill -9 -f "qmus_$TAG" 2>/dev/null; pkill -9 -f "copygate_hook" 2>/dev/null
echo "DONE $TAG"
