#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
PLUG=$ROOT/build/contrib/plugins/libpctrace.so; OUT=/home/dmontero/6g/remote
cd $ROOT; IR='python3 tools/ipod_classic/iremote.py'
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
PA="nodefault=on,always=on,cap=4,pc=0x08150d84:StartPlayback,pc=0x0808170c:audio_engine,pc=0x08016e44:copyPCM,pc=0x08016eb4:copy32_REALPCM,pc=0x080a3c00:recv3c00,memwpc=0x08016eb4:copy32,out=/home/dmontero/6g/play.log"
export AUDIOPACE=1
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -plugin "$PLUG,$PA" >$OUT/qmus.out 2>$OUT/qmus.err &
echo boot28; sleep 28
$IR wake e 3000 >/dev/null 2>&1
$IR shot F0 >/dev/null 2>&1
python3 tools/ipod_classic/pixcount.py $OUT/F0.ppm 2>/dev/null | grep -oP 'non-black: \K[0-9]+'|head -1
cp $OUT/F0.png /mnt/c/Desarrollo/qemu-ios/F0.png 2>/dev/null
# fast: menu-up once, 6 down, select
$IR seq w:120 e:120 e:120 e:120 e:120 e:120 e:120 FAST >/dev/null 2>&1
cp $OUT/FAST.png /mnt/c/Desarrollo/qemu-ios/FAST.png 2>/dev/null
$IR press ret 200 -u -w e -r 8 -t 1500 -s 1 --name FSEL >/dev/null 2>&1
cp $OUT/FSEL.png /mnt/c/Desarrollo/qemu-ios/FSEL.png 2>/dev/null
sleep 4
echo "SP hits: $(grep -c 'StartPlayback @' /home/dmontero/6g/play.log 2>/dev/null)"
pgrep -x qmus >/dev/null && $IR raw quit >/dev/null 2>&1; sleep 2; pkill -9 -x qmus 2>/dev/null
echo '=== SUMMARY ==='; sed -n '/PCTRACE summary/,$p' /home/dmontero/6g/play.log 2>/dev/null
