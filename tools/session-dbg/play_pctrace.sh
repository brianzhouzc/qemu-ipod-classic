#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
PLUG=$ROOT/build/contrib/plugins/libpctrace.so; OUT=/home/dmontero/6g/remote
cd $ROOT; IR='python3 tools/ipod_classic/iremote.py'
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
PA="nodefault=on,always=on,cap=4,pc=0x08150d84:StartPlayback,pc=0x0808170c:audio_engine,pc=0x08016e44:copyPCM,pc=0x08016eb4:copy32_REALPCM,pc=0x080a3c00:recv3c00,memw=0x22008ca4-0x22008ca5,memwpc=0x08016eb4:copy32,out=/home/dmontero/6g/play.log"
export AUDIOPACE=1
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -plugin "$PLUG,$PA" >$OUT/qmus.out 2>$OUT/qmus.err &
for t in $(seq 1 22); do
  sleep 3; pgrep -x qmus >/dev/null || { echo "died poll $t"; break; }
  $IR wake e 2500 >/dev/null 2>&1; $IR shot PP$t >/dev/null 2>&1
  nb=$(python3 tools/ipod_classic/pixcount.py $OUT/PP$t.ppm 2>/dev/null | grep -oP 'non-black: \K[0-9]+'|head -1)
  echo "poll $t nb=${nb:-0}"; [ "${nb:-0}" -gt 40000 ] && { echo READY; break; }
done
pgrep -x qmus >/dev/null || { echo 'DIED before drive'; exit 1; }
$IR seq w:150 wait:1 TOPP >/dev/null 2>&1
$IR seq e:130 wait:1 e:130 wait:1 e:130 wait:1 e:130 wait:1 e:130 wait:1 e:130 wait:1 ATS >/dev/null 2>&1
cp $OUT/ATS.png /mnt/c/Desarrollo/qemu-ios/ATS.png 2>/dev/null
$IR press ret 250 -u -w e -r 15 -t 1500 -s 2 --name PLAYSEL 2>&1 | tail -1
cp $OUT/PLAYSEL.png /mnt/c/Desarrollo/qemu-ios/PLAYSEL.png 2>/dev/null
sleep 6
echo "--- StartPlayback hits ---"; grep -c 'StartPlayback @' /home/dmontero/6g/play.log 2>/dev/null
pgrep -x qmus >/dev/null && $IR raw quit >/dev/null 2>&1
sleep 2; pkill -9 -x qmus 2>/dev/null
echo '===== PCTRACE SUMMARY ====='; sed -n '/PCTRACE summary/,$p' /home/dmontero/6g/play.log 2>/dev/null
echo '===== window hits ====='; grep -E 'StartPlayback|audio_engine|copyPCM|copy32|recv3c00|MEMW' /home/dmontero/6g/play.log 2>/dev/null | grep -v watch | head -20
