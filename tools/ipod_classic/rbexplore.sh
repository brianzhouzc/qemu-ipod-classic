#!/bin/sh
# rbexplore.sh <disk> <outdir>
# Boot Rockbox, enter Files (SELECT on the top item), then wheel forward a few
# times, screendumping the file browser at each step so we can SEE the listing
# (does the FAT mount? is the MP3 there? which click lands on it?).
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
qemu="$root/build/qemu-system-arm"
disk=${1:-$HOME/6g/Erbmusic.img}
out=${2:-$HOME/6g/rbexplore}
bin="$root/assets/rockbox/build-full/rockbox.bin"
mkdir -p "$out"; rm -f "$out"/*.ppm "$out"/*.png "$out"/qemu.err
pkill -9 -x qemu-system-arm 2>/dev/null || true
sleep 1
{
    sleep 16; echo "screendump $out/0-menu.ppm"
    sleep 2;  echo "sendkey ret 150"        # SELECT -> enter Files
    sleep 3;  echo "screendump $out/1-files.ppm"
    sleep 1;  echo "sendkey e 150"          # wheel fwd
    sleep 2;  echo "screendump $out/2-f1.ppm"
    sleep 1;  echo "sendkey e 150"
    sleep 2;  echo "screendump $out/3-f2.ppm"
    sleep 1;  echo "sendkey e 150"
    sleep 2;  echo "screendump $out/4-f3.ppm"
    sleep 1;  echo quit
} | I2STRACE=1 "$qemu" -M iPod-Classic \
        -kernel "$bin" \
        -drive "if=ide,format=raw,file=$disk,file.locking=off" \
        -snapshot \
        -display none -serial null -monitor stdio \
        >/dev/null 2>"$out/qemu.err" || true
# ppm -> png (one at a time; the converter takes src dst)
for p in "$out"/*.ppm; do
    python3 "$root/tools/ipod_classic/ppm2png.py" "$p" "${p%.ppm}.png" || true
done
echo "=== I2S accesses ==="
grep -c I2STRACE "$out/qemu.err" 2>/dev/null || echo 0
echo "done: $out"
