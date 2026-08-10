#!/bin/sh
# Longer runs, to see whether the "diskmode" software reset is disk-dependent
# or just a race.  Scratch tool.
set -e
repo=/mnt/c/Desarrollo/qemu-ios
w=$HOME/6g
for v in A zero; do
    for r in 1 2 3; do
        sh "$repo/tools/ipod_classic/atatrace.sh" "$w/$v.img" "$w/L$v.$r.txt" 45 >/dev/null
        lines=$(wc -l < "$w/L$v.$r.txt")
        res=$(grep -c "software reset" "$w/L$v.$r.txt" || true)
        dma=$(grep -c "0x70 = 0x000000c8" "$w/L$v.$r.txt" || true)
        img=$(grep -o 'next image "[^"]*"' "$w/L$v.$r.txt" | sort -u | tr '\n' ' ')
        echo "$v run$r  lines=$lines resets=$res dma=$dma $img"
    done
done
