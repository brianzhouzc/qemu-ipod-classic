#!/bin/sh
# Run the retailOS on the iPod-Classic machine under QEMU's gdbstub and drive it
# with gdbpoke.py.  Everything lives in WSL's $HOME because /tmp does not
# survive between wsl invocations.
#
#   gdbrun.sh <port> <disk-image|none> <run-seconds> [gdbpoke args...]
#
# e.g.  sh gdbrun.sh 1300 none 90 --break 0x0815b1b8 --hits 4
set -e
PORT=$1; DISK=$2; SECS=$3; shift 3
ROOT=/mnt/c/Desarrollo/qemu-ios
OSOS=$ROOT/assets/classic6g/osos_decrypted.bin

pkill -9 -f qemu-system-arm 2>/dev/null || true
sleep 1

DRIVE=""
[ "$DISK" != "none" ] && DRIVE="-drive if=ide,format=raw,file=$DISK"

cd $ROOT/build
# shellcheck disable=SC2086
nohup timeout "$SECS" ./qemu-system-arm -M iPod-Classic -kernel "$OSOS" \
    -display none -monitor none -serial null $DRIVE $QEMU_EXTRA \
    -S -gdb tcp::"$PORT" >/dev/null 2>"$HOME/6g/qemu.$PORT.err" &
sleep 3
cd $ROOT
timeout "$SECS" python3 tools/ipod_classic/gdbpoke.py --port "$PORT" "$@" || true
pkill -9 -f qemu-system-arm 2>/dev/null || true
echo "--- qemu stderr ---"
head -20 "$HOME/6g/qemu.$PORT.err"
