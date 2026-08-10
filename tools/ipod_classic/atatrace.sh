#!/bin/sh
# atatrace.sh <disk-image> <out-file> [seconds]
# Boot the retailOS on the iPod-Classic machine with the given disk and capture
# every ATA register access plus anything the firmware says by semihosting.
set -e
img=$1
out=$2
secs=${3:-20}
cd "$(dirname "$0")/../../build"
ATATRACE=1 timeout "$secs" ./qemu-system-arm \
    -M iPod-Classic \
    -kernel ../assets/classic6g/osos_decrypted.bin \
    -display none -monitor none -serial null \
    ${img:+-drive if=ide,format=raw,file=$img} \
    >"$out" 2>&1 || true
wc -l "$out"
