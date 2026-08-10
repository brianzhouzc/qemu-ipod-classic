#!/bin/sh
# ranaddr.sh <disk-image|""> <seconds> <addr> [addr...]
# "Did this function run?"  Only valid for addresses that are block entries
# (a function prologue) -- see the warning in wiki/11 section 7.
set -e
repo=/mnt/c/Desarrollo/qemu-ios
img=$1; secs=$2; shift 2
cd "$repo/build"
log=$(mktemp)
timeout "$secs" ./qemu-system-arm -M iPod-Classic \
    -kernel ../assets/classic6g/osos_decrypted.bin \
    -display none -monitor none -serial null \
    ${img:+-drive if=ide,format=raw,file=$img} \
    -d in_asm -D "$log" >/dev/null 2>&1 || true
echo "log lines: $(wc -l < "$log")"
for a in "$@"; do
    echo "$a  $(grep -c "^$a" "$log" || true)"
done
rm -f "$log"
