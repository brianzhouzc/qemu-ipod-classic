#!/bin/sh
# navsongs.sh <qemu-bin> <disk> <outdir> [extra qemu args]
# Boots retailOS, drives language -> main menu -> Music -> Songs, taking a
# screenshot at every step so the path can be mapped even when a SELECT is
# dropped (the known ~5/10 flake; wiki/11 s.18.8). SELECT is held 250 ms.
set -e
QEMU=$1; DISK=$2; OUT=$3; shift 3
ROOT=/mnt/c/Desarrollo/qemu-ios
OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
mkdir -p "$OUT"; rm -f "$OUT"/*.ppm
pkill -9 -x qemu-system-arm 2>/dev/null || true
pkill -9 -x qtmp 2>/dev/null || true
sleep 1
cp "$QEMU" "$ROOT/build/qtmp"
{
    sleep 48;  echo "screendump $OUT/00-lang.ppm"
    sleep 2;   echo "sendkey e 150"
    sleep 6;   echo "sendkey e 150"
    sleep 6;   echo "sendkey q 150"
    sleep 4;   echo "sendkey ret 250"          # SELECT: language -> main menu
    sleep 24;  echo "screendump $OUT/01-main.ppm"
    sleep 2;   echo "sendkey ret 250"          # SELECT: main -> Music submenu
    sleep 12;  echo "screendump $OUT/02-music.ppm"
    sleep 2;   echo "sendkey e 150"
    sleep 4;   echo "screendump $OUT/03-e1.ppm"
    sleep 2;   echo "sendkey e 150"
    sleep 4;   echo "screendump $OUT/04-e2.ppm"
    sleep 2;   echo "sendkey e 150"
    sleep 4;   echo "screendump $OUT/05-e3.ppm"
    sleep 2;   echo "sendkey e 150"
    sleep 4;   echo "screendump $OUT/06-e4.ppm"
    sleep 2;   echo "sendkey ret 250"          # SELECT: (Songs) -> song list
    sleep 12;  echo "screendump $OUT/07-select.ppm"
    sleep 2;   echo quit
} | DISPTRACE=1 "$ROOT/build/qtmp" -M iPod-Classic \
        -kernel "$OSOS" \
        -drive "if=ide,format=raw,file=$DISK,file.locking=off" \
        -snapshot \
        -display none -serial null -monitor stdio "$@" \
        >/dev/null 2>"$OUT/qemu.err" || true
pkill -9 -x qtmp 2>/dev/null || true
# PPM -> PNG for every shot.
for f in "$OUT"/*.ppm; do
    [ -f "$f" ] && python3 "$ROOT/tools/ipod_classic/ppm2png.py" "$f" "${f%.ppm}.png" 2>/dev/null || true
done
echo "resets:"; grep -c "software reset" "$OUT/qemu.err" 2>/dev/null || echo 0
ls -1 "$OUT"/*.png 2>/dev/null
