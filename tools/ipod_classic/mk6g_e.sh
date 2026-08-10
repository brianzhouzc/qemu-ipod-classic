#!/bin/sh
set -e
repo=/mnt/c/Desarrollo/qemu-ios
w=$HOME/6g
python3 "$repo/tools/ipod_classic/mkdisk6g4k.py" "$w/E.img" \
    "$repo/assets/extracted/Firmware-24.9.1.2" "$w/fat32.img"
for r in 1 2; do
    sh "$repo/tools/ipod_classic/atatrace.sh" "$w/E.img" "$w/tE.$r.txt" 30 >/dev/null
    echo "E run$r lines=$(wc -l < "$w/tE.$r.txt") dma=$(grep -c '0x70 = 0x000000c8' "$w/tE.$r.txt" || true) $(grep -o 'next image "[^"]*"' "$w/tE.$r.txt" | sort -u | tr '\n' ' ')"
done
echo "--- taskfile ---"
grep -n '0x5c =\|0x60 =\|0x64 =\|0x68 =\|0x70 = 0x000000c8' "$w/tE.1.txt" | tail -30
