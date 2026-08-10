#!/bin/bash
# run.sh PORT DISK SECS -- then runs $HOME/6g/w/cmd.sh contents as the client
PORT=$1; DISK=$2; SECS=$3; shift 3
pkill -9 -x qemu-system-arm 2>/dev/null
sleep 1
DRIVE=""
[ "$DISK" != "none" ] && DRIVE="-drive if=ide,format=raw,file=$DISK"
cd /mnt/c/Desarrollo/qemu-ios/build
nohup timeout "$SECS" ./qemu-system-arm -M iPod-Classic \
  -kernel ../assets/classic6g/osos_decrypted.bin \
  -display none -monitor none -serial null $DRIVE $QEMU_EXTRA \
  -S -gdb tcp::$PORT >/dev/null 2>$HOME/6g/e.$PORT.txt &
sleep 3
timeout "$SECS" python3 "$@"
RC=$?
pkill -9 -x qemu-system-arm 2>/dev/null
echo "--- client rc=$RC; qemu stderr head ---"
head -8 $HOME/6g/e.$PORT.txt
