#!/bin/sh
# ctrlwatch.sh <gport> <disk> <secs> -- control: read-watchpoint on the LBA0
# detector buffer offset +0x1FE, which the detector reads at boot.
set -e
GPORT=$1; DISK=$2; SECS=$3
ROOT=/mnt/c/Desarrollo/qemu-ios
OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
pkill -9 -x qemu-system-arm 2>/dev/null || true
sleep 1
cd "$ROOT/build"
nohup timeout "$SECS" ./qemu-system-arm -M iPod-Classic -kernel "$OSOS" \
    -drive "if=ide,format=raw,file=$DISK,file.locking=off" -snapshot \
    -display none -serial null -monitor none \
    -S -gdb tcp::$GPORT >/dev/null 2>"$HOME/6g/ctrl.err" &
sleep 3
cd "$ROOT"
timeout "$SECS" python3 tools/ipod_classic/ctrlwatch.py "$GPORT" || true
pkill -9 -x qemu-system-arm 2>/dev/null || true
