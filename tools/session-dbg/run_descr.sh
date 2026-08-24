#!/bin/bash
# Measure the descriptor the NATURAL setup-time render passes to the copy
# routine 0x08016e44 (r2 = the [r4] descriptor; [r7+5] gate lives at r7=[sp+32]).
# If r2 == 0x0bb12d54 the stable heap descriptor is used and a passive prefill
# can prime the copy without hijacking the CPU. If it is a stack address the
# prefill must target that instead.
set +e
TAG=${1:-descr}
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
PA="nodefault=on,always=on,cap=6,window=6000,out=$LOG,regcap=40"
PA="$PA,pc=0x08150d84:StartPlayback,pc=0x0808170c:engine,pc=0x08016eb4:copy32"
PA="$PA,regpc=0x08016e44:copyent,regpc=0x0808170c:engent"
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
  if grep -aqE 'REGPC (copyent|engent)' "$LOG" 2>/dev/null; then echo "PROGRESS try $i"; break; fi
done
echo "=== 8s free-run ==="; sleep 8
echo "=== [$TAG] copy-entry descriptors (r2 = [r4] descr; want 0x0bb12d54) ==="
grep -aE 'REGPC copyent' "$LOG" | head -16
echo "=== [$TAG] engine entry ==="
grep -aE 'REGPC engent' "$LOG" | head -6
echo "=== [$TAG] copy32 hits ==="
grep -aE 'copy32 .*hits=' "$LOG"
exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 2 && exec 3<&-
sleep 2
pkill -9 -f "qmus_$TAG" 2>/dev/null
echo "DONE $TAG"
