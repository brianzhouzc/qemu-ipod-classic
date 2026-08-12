#!/bin/sh
# navmusic.sh <qemu-bin> <disk> <errfile> [extra qemu args]
# Boots retailOS, navigates language -> main menu -> Music, quits.
# Captures stderr (FATPROBE / DISPTRACE lines) to <errfile>.
set -e
QEMU=$1; DISK=$2; ERR=$3; shift 3
ROOT=/mnt/c/Desarrollo/qemu-ios
OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
pkill -9 -x qemu-system-arm 2>/dev/null || true
pkill -9 -x qtmp 2>/dev/null || true
sleep 1
cp "$QEMU" "$ROOT/build/qtmp"
{
    sleep 48;  echo "sendkey e 150"
    sleep 6;   echo "sendkey e 150"
    sleep 6;   echo "sendkey q 150"
    sleep 6;   echo "sendkey ret 150"   # SELECT: language -> main menu
    sleep 28;  echo "sendkey e 150"
    sleep 8;   echo "sendkey q 150"
    sleep 8;   echo "sendkey ret 150"   # SELECT: into Music
    sleep 16;  echo "screendump $ROOT/build/navmusic.ppm"
    sleep 2;   echo quit
} | DISPTRACE=1 "$ROOT/build/qtmp" -M iPod-Classic \
        -kernel "$OSOS" \
        -drive "if=ide,format=raw,file=$DISK,file.locking=off" \
        -snapshot \
        -display none -serial null -monitor stdio "$@" \
        >/dev/null 2>"$ERR" || true
pkill -9 -x qtmp 2>/dev/null || true
echo "=== FATPROBE lines ==="
grep FATPROBE "$ERR" || echo "(none)"
echo "=== resets ==="
grep -c "software reset" "$ERR" || true
