#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
PLUG=$ROOT/build/contrib/plugins/libpctrace.so; OUT=/home/dmontero/6g/remote
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/sm1v2.log; rm -f $LOG
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
PA="nodefault=on,always=on,cap=4,out=$LOG,pc=0x08150d84:StartPlayback"
PA="$PA,pc=0x0808af58:setupbody,pc=0x0808af7c:bit12yes,pc=0x0808afa0:common"
PA="$PA,pc=0x0809a72c:en858,pc=0x080a35a0:en804"
PA="$PA,pc=0x0808170c:engine,pc=0x08016eb4:copy32,pc=0x08016e44:copyfn"
PA="$PA,pc=0x08123970:h12,pc=0x08123dcc:disp,pc=0x0809a74c:mixer"
PA="$PA,sendex=0x22003f7c:0:0x52:sel52"
export AUDIOPACE=1 SM1_ENGINE=${SM1_ENGINE:-1}
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234 -plugin "$PLUG,$PA" >$OUT/qmus.out 2>$OUT/qmus.err &
echo "launched pid $! SM1_ENGINE=$SM1_ENGINE"
for t in $(seq 1 22); do
  sleep 3; pgrep -x qmus >/dev/null || { echo "died poll $t"; break; }
  $IR wake e 2500 >/dev/null 2>&1; $IR shot PP$t >/dev/null 2>&1
  nb=$(python3 tools/ipod_classic/pixcount.py $OUT/PP$t.ppm 2>/dev/null | grep -oP "non-black: \K[0-9]+"|head -1)
  echo "poll $t nb=${nb:-0}"; [ "${nb:-0}" -gt 40000 ] && { echo READY; break; }
done
$IR seq w:150 wait:1 TOPP >/dev/null 2>&1
$IR seq e:130 wait:1 e:130 wait:1 e:130 wait:1 e:130 wait:1 e:130 wait:1 e:130 wait:1 ATS >/dev/null 2>&1
$IR press ret 250 -u -w e -r 15 -t 1500 -s 2 --name PLAYSEL 2>&1 | tail -1
for r in 1 2 3 4 5 6; do
  grep -aq "08150d84  #" $LOG && break
  $IR seq e:130 ret:220 wait:1 >/dev/null 2>&1
done
sleep 14
exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 2 && exec 3<&-
sleep 2
echo "=== COUNTERS ==="; sed -n "/PCTRACE summary/,\$p" $LOG | grep -aE "StartPlayback|setupbody|bit12yes|common|en858|en804|engine |copy32|copyfn|h12 |disp |mixer|sel52"
echo "resets=$(grep -c "software reset" $OUT/qmus.err)"
pkill -9 -x qmus 2>/dev/null
echo DONE
