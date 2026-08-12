#!/bin/sh
# fatrun.sh <port> <disk> <secs> <outfile> <fatprobe args...>
set -e
PORT=$1; DISK=$2; SECS=$3; OUT=$4; shift 4
ROOT=/mnt/c/Desarrollo/qemu-ios
OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
pkill -9 -x qemu-system-arm 2>/dev/null || true
sleep 1
mkdir -p "$HOME/6g"
DRIVE=""
[ "$DISK" != "none" ] && DRIVE="-drive if=ide,format=raw,file=$DISK"
cd "$ROOT/build"
# shellcheck disable=SC2086
nohup timeout "$SECS" ./qemu-system-arm -M iPod-Classic -kernel "$OSOS" \
    -display none -monitor none -serial null $DRIVE $QEMU_EXTRA \
    -S -gdb tcp::"$PORT" >/dev/null 2>"$HOME/6g/qemu.$PORT.err" &
sleep 3
cd "$ROOT"
timeout "$SECS" python3 tools/ipod_classic/fatprobe.py --port "$PORT" "$@" >"$OUT" 2>&1 || true
pkill -9 -x qemu-system-arm 2>/dev/null || true
echo "done -> $OUT"
