#!/bin/sh
# Build several 6G disk-layout variants and trace each one.  Scratch tool.
set -e
repo=/mnt/c/Desarrollo/qemu-ios
w=$HOME/6g
mkdir -p "$w"
export MTOOLS_SKIP_CHECK=1

# A  = MBR + real firmware partition (ipsw) + FAT32 with iPod_Control  (already built)
# B  = MBR + zero-filled firmware partition of the same size + same FAT32
# C  = MBR + real firmware partition + empty FAT32 (no iPod_Control)
# D  = firmware partition raw at LBA 0, no MBR
if [ ! -f "$w/fwzero.img" ]; then
    truncate -s 92930048 "$w/fwzero.img"
fi
if [ ! -f "$w/fat32empty.img" ]; then
    truncate -s 200M "$w/fat32empty.img"
    mkfs.vfat -F 32 -n IPOD "$w/fat32empty.img" >/dev/null
fi

python3 "$repo/tools/ipod_classic/mkdisk6g.py" "$w/B.img" "$w/fwzero.img" "$w/fat32.img" 63
python3 "$repo/tools/ipod_classic/mkdisk6g.py" "$w/C.img" \
    "$repo/assets/extracted/Firmware-24.9.1.2" "$w/fat32empty.img" 63
cp "$repo/assets/extracted/Firmware-24.9.1.2" "$w/D.img"

for v in A B C D zero; do
    for run in 1 2; do
        sh "$repo/tools/ipod_classic/atatrace.sh" "$w/$v.img" "$w/t$v.$run.txt" 20 >/dev/null
        n=$(grep -c "software reset" "$w/t$v.$run.txt" || true)
        img=$(grep -o 'next image "[^"]*"' "$w/t$v.$run.txt" | sort -u | tr '\n' ' ')
        dma=$(grep -c "ATA WR +0x70 = 0x000000c8" "$w/t$v.$run.txt" || true)
        lines=$(wc -l < "$w/t$v.$run.txt")
        echo "$v run$run: lines=$lines resets=$n dma_reads=$dma $img"
    done
done
