#!/bin/sh
# Rebuild the 6G experiment images and capture ATA traces.  Scratch tool for
# the disk-layout investigation; writes everything under $HOME/6g.
set -e
repo=/mnt/c/Desarrollo/qemu-ios
w=$HOME/6g
mkdir -p "$w"

export MTOOLS_SKIP_CHECK=1

if [ ! -f "$w/fat32.img" ]; then
    truncate -s 200M "$w/fat32.img"
    mkfs.vfat -F 32 -n IPOD "$w/fat32.img" >/dev/null
    for d in ::/iPod_Control ::/iPod_Control/Music ::/iPod_Control/iTunes \
             ::/iPod_Control/Device ::/iPod_Control/Artwork; do
        mmd -i "$w/fat32.img" $d
    done
    for i in 00 01 02 03; do mmd -i "$w/fat32.img" ::/iPod_Control/Music/F$i; done
fi

if [ ! -f "$w/zero.img" ]; then
    truncate -s 64M "$w/zero.img"
fi

python3 "$repo/tools/ipod_classic/mkdisk6g.py" "$w/A.img" \
    "$repo/assets/extracted/Firmware-24.9.1.2" "$w/fat32.img" 63

ls -la "$w"
