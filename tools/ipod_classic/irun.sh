#!/bin/sh
# irun.sh <disk> [extra qemu args]
# Boot the 6G retailOS detached under a uniquely named QEMU copy (build/qmus,
# so navtest.sh's `pkill -9 -x qemu-system-arm`/`-x qtmp` never touches it) with
# a human monitor on TCP 127.0.0.1:4455 for iremote.py to drive. Screendumps and
# logs land in ~/6g/remote/. Kill it with: pkill -9 -x qmus
set -e
DISK=$1; shift 2>/dev/null || true
ROOT=/mnt/c/Desarrollo/qemu-ios
OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote
PORT=${IREMOTE_PORT:-4455}
mkdir -p "$OUT"
pkill -9 -x qmus 2>/dev/null || true
sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
setsid "$ROOT/build/qmus" -M iPod-Classic \
    -kernel "$OSOS" \
    -drive "if=ide,format=raw,file=$DISK,file.locking=off" \
    -snapshot \
    -display none -serial null \
    -monitor "tcp:127.0.0.1:$PORT,server,nowait" \
    "$@" >"$OUT/qemu.out" 2>"$OUT/qemu.err" &
echo $! > "$OUT/qmus.pid"
echo "launched qmus pid $(cat "$OUT/qmus.pid") disk=$DISK monitor 127.0.0.1:$PORT"
