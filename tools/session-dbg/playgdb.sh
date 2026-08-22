#!/bin/bash
# Launch retailOS with BOTH the human monitor (iremote nav on 4455) and the gdb
# stub (inspection on 1234), running free (no -S). AUDIOPACE=1. Snapshot disk.
set -e
DISK=${1:-/home/dmontero/6g/Emulti.img}
GDBPORT=${2:-1234}
MONPORT=${3:-4455}
WAV=${4:-}
ROOT=/mnt/c/Desarrollo/qemu-ios
OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote
mkdir -p "$OUT"
pkill -9 -x qmus 2>/dev/null || true
sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1
[ -n "$WAV" ] && export AUDIOWAV="$WAV"
setsid "$ROOT/build/qmus" -M iPod-Classic \
    -kernel "$OSOS" \
    -drive "if=ide,format=raw,file=$DISK,file.locking=off" \
    -snapshot \
    -display none -serial null \
    -monitor "tcp:127.0.0.1:$MONPORT,server,nowait" \
    -gdb "tcp::$GDBPORT" \
    >"$OUT/qmus.out" 2>"$OUT/qmus.err" &
echo $! > "$OUT/qmus.pid"
echo "launched qmus pid $(cat "$OUT/qmus.pid") disk=$DISK gdb=$GDBPORT mon=$MONPORT wav=${WAV:-none}"
