#!/bin/bash
# Assault #13 phase A: instruction-timing fidelity sweep with -icount.
#
# The MeCCA bootstrap deadlock is a race sensitive to guest speed (wiki/19
# s11.11-11.12: regime A/B flips with instrumentation weight). On real silicon
# (ARM926 @ 216 MHz) the same firmware never deadlocks, so the divergence is
# timing/order, not a missing register. -icount makes virtual time a pure
# function of executed instructions: deterministic runs, and a knob (shift)
# to sweep the effective MIPS rate around the real one (~240 MIPS ~ shift=2).
#
# Usage: run_icount.sh <shift|off> [tag]
#   shift=N  -> -icount shift=N,sleep=on  (sleep=on keeps wall ~ virtual so
#               the screen-driven navigation below behaves as in runmeas.sh)
#   off      -> baseline, no icount (reproduce the deadlock on this machine)
#
# Counters printed at the end are the runmeas.sh set: disp/h12/dwork/
# gateconsume/copy32/ex6/mbox52sel. Success signature = copy32 > 0.
set +e
SHIFT=${1:?usage: run_icount.sh <shift|off> [tag]}
TAG=${2:-ic$SHIFT}
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
PA="nodefault=on,always=on,cap=6,window=4000,out=$LOG"
PA="$PA,pc=0x081fa9e4:Shuffle,pc=0x08150d84:StartPlayback"
PA="$PA,pc=0x08123dcc:disp,pc=0x08123970:h12,pc=0x08123bfc:dwork,pc=0x081239bc:gateconsume,pc=0x08016eb4:copy32"
PA="$PA,pc=0x08123508:h12else,pc=0x0809a74c:mixer,pc=0x0808170c:engine"
PA="$PA,sendex=0x22004138:3:0x6:ex6,sendex=0x22003f7c:0:0x52:mbox52sel"

ICOUNT=()
if [ "$SHIFT" != "off" ]; then
  ICOUNT=(-icount "shift=$SHIFT,sleep=on" -rtc clock=vm)
fi

setsid "$BIN" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=$IMG,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234 \
  "${ICOUNT[@]}" \
  -plugin "$PLUG,$PA" >"$OUT/qmus_$TAG.out" 2>"$OUT/qmus_$TAG.err" &
echo "launched $TAG pid $!"
sleep 52
$IR wake e >/dev/null 2>&1
# Screen-driven navigation with retries (robust to icount changing wall pace).
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name L1 >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:2 L2 >/dev/null 2>&1
CONF=0
for i in 1 2 3 4 5 6 7 8; do
  $IR seq e:120 wait:1 ret:250 wait:2 z >/dev/null 2>&1
  if grep -aqE 'cpu[0-9]  (h12|ex6) ' "$LOG" 2>/dev/null; then echo "PROGRESS try $i"; CONF=1; break; fi
done
echo "=== 10s free-run ==="; sleep 10
echo "=== [$TAG] counters ==="
grep -aE 'cpu[0-9]  (StartPlayback|Shuffle|disp|h12|h12else|dwork|gateconsume|copy32|mixer|engine|ex6|mbox52sel) ' "$LOG" | head -30
exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 2 && exec 3<&-
sleep 2
echo "=== [$TAG] PCTRACE summary ==="
sed -n '/PCTRACE summary/,$p' "$LOG" | head -25
echo "resets=$(grep -c 'software reset' "$OUT/qmus_$TAG.err")"
pkill -9 -f "qmus_$TAG" 2>/dev/null
echo "DONE $TAG"
