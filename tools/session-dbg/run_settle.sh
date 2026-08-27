#!/bin/bash
# Does the library appear if the device is simply LEFT ALONE?
#
# With the SHA-1 engine wired the boot is clean -- no CPU_exception_handler, no
# software reset -- but the Music entry reports no songs at the point every other
# harness looks. Those harnesses navigate almost immediately and shoot early, and
# an early shot has already been caught lying once: the same pristine image read
# "4 skladeb" in one run and "none" in another purely because the database was
# still loading.
#
# So this one does nothing at all except dismiss the language picker and then
# watch the same screen at 30 s intervals out to four minutes. If the count ever
# turns non-zero, the library is merely slow here and the reject theory is wrong.
set +e
TAG=${1:-settle}
ROOT=$HOME/qemu-ios
OUT=$HOME/6g/remote; mkdir -p "$OUT"
export IREMOTE_OUT=$OUT
cd "$ROOT" || exit 1
IR="python3 tools/ipod_classic/iremote.py"

pkill -9 -f 'build/qmus[_]' 2>/dev/null
for _ in $(seq 1 30); do ss -ltn 2>/dev/null | grep -q ":4455 " || break; sleep 2; done

BIN=$ROOT/build/qmus_$TAG
cp "$ROOT/build/qemu-system-arm" "$BIN"
setsid "$BIN" -M iPod-Classic -kernel "$ROOT/assets/classic6g/osos_decrypted.bin" \
  -drive "if=ide,format=raw,file=$HOME/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" \
  >"$OUT/$TAG.out" 2>"$OUT/$TAG.err" &

sleep 50
$IR wake e >/dev/null 2>&1
sleep 2
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name ${TAG}_lang >/dev/null 2>&1

for t in 30 60 120 180 240; do
  sleep 30
  $IR shot ${TAG}_t$t >/dev/null 2>&1
done

exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 3 && exec 3<&-
echo "resets=$(grep -ac 'software reset' "$OUT/$TAG.err")"
echo "DONE $TAG"
