#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
PLUG=$ROOT/build/contrib/plugins/libpctrace.so; OUT=/home/dmontero/6g/remote
cd $ROOT; IR='python3 tools/ipod_classic/iremote.py'
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
PA="nodefault=on,always=on,cap=3,pc=0x08150d84:StartPlayback,pc=0x0808170c:audio_engine,pc=0x0809a74c:mixer,pc=0x08016eb4:copy32REAL"
PA="$PA,pc=0x08033af8:KS_signal,pc=0x080cc0e4:KS_post,pc=0x080c8968:KS_rpc,pc=0x081f87f8:media_dispatch,pc=0x080a3c00:recv3c00"
PA="$PA,out=/home/dmontero/6g/ks.log"
export AUDIOPACE=1
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -plugin "$PLUG,$PA" >$OUT/qmus.out 2>$OUT/qmus.err &
sleep 28
$IR wake e 3000 >/dev/null 2>&1
pgrep -x qmus >/dev/null || { echo DIED; exit 1; }
$IR seq w:120 e:120 e:120 e:120 e:120 e:120 e:120 KS >/dev/null 2>&1
$IR press ret 200 -u -w e -r 8 -t 1500 -s 1 --name KSSEL >/dev/null 2>&1
cp $OUT/KSSEL.png /mnt/c/Desarrollo/qemu-ios/KSSEL.png 2>/dev/null
sleep 5
echo "SP: $(grep -c 'StartPlayback @' /home/dmontero/6g/ks.log)"
pgrep -x qmus >/dev/null && $IR raw quit >/dev/null 2>&1; sleep 2; pkill -9 -x qmus 2>/dev/null
echo '=== SUMMARY ==='; sed -n '/PCTRACE summary/,$p' /home/dmontero/6g/ks.log 2>/dev/null
