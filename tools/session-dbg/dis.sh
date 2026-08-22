#!/bin/bash
# dis.sh ADDR [LEN] [arm|thumb]
ADDR=$(printf "%d" $1); LEN=${2:-96}; MODE=${3:-arm}
IRAM=/home/dmontero/6g/iram.bin
OSOS=/mnt/c/Desarrollo/qemu-ios/assets/classic6g/osos_decrypted.bin
if [ $ADDR -ge $((0x22000000)) ] && [ $ADDR -lt $((0x2200c000)) ]; then
  off=$((ADDR - 0x22000000)); F=$IRAM
else
  off=$((ADDR - 0x07FF51B8)); F=$OSOS
fi
T=$(mktemp)
dd if=$F of=$T bs=1 skip=$off count=$LEN status=none
M="-m arm"; [ "$MODE" = "thumb" ] && M="-m arm -M force-thumb"
arm-none-eabi-objdump -D -b binary $M -EL --adjust-vma=$ADDR $T | grep -E ":\s+[0-9a-f]{2}"
rm -f $T
