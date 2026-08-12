#!/bin/sh
# fatwatchrun.sh <gport> <mport> <disk> <secs> <outfile>
set -e
GPORT=$1; MPORT=$2; DISK=$3; SECS=$4; OUT=$5
ROOT=/mnt/c/Desarrollo/qemu-ios
OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
pkill -9 -x qemu-system-arm 2>/dev/null || true
sleep 1
cd "$ROOT/build"
nohup timeout "$SECS" ./qemu-system-arm -M iPod-Classic -kernel "$OSOS" \
    -drive "if=ide,format=raw,file=$DISK,file.locking=off" -snapshot \
    -display none -serial null \
    -monitor tcp:127.0.0.1:$MPORT,server,nowait \
    -S -gdb tcp::$GPORT >/dev/null 2>"$HOME/6g/fatwatch.qemu.err" &
sleep 3
cd "$ROOT"
timeout "$SECS" python3 tools/ipod_classic/fatwatch.py --gport "$GPORT" \
    --mport "$MPORT" --timeout $((SECS-5)) >"$OUT" 2>&1 || true
pkill -9 -x qemu-system-arm 2>/dev/null || true
echo "done -> $OUT"
grep FATPROBE "$HOME/6g/fatwatch.qemu.err" | head
