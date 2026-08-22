#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
PA="nodefault=on,always=on,cap=4,pc=0x08150d84:StartPlayback,pc=0x0808170c:audio_engine,out=/home/dmontero/6g/wg.log"
export AUDIOPACE=1
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS"   -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot   -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait"   -gdb tcp::1234   -plugin "$PLUG,$PA" >$OUT/qmus.out 2>$OUT/qmus.err &
echo "launched pid $!"
