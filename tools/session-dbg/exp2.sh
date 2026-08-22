#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin; OUT=/home/dmontero/6g/remote
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
PA="nodefault=on,always=on,cap=8,pc=0x08150d84:StartPlayback,pc=0x0808170c:audio_engine,pc=0x08016eb4:copy32_REALPCM,memwpc=0x08016eb4:copy32,out=/home/dmontero/6g/exp2.log"
# AUDIOPACE, AUDIOPACE_START_MS, AUDIOWAV inherited from caller env
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" \
  -plugin "$PLUG,$PA" >"$OUT/qmus.out" 2>"$OUT/qmus.err" &
sleep 48
$IR wake e 4000 >/dev/null 2>&1
# language -> main: retry harder
$IR press ret 250 -u -w e --region right -r 12 -t 3000 -s 4 --name E2a >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:2 E2b >/dev/null 2>&1
# SELECT shuffle songs several times, checking StartPlayback between
for t in 1 2 3 4 5; do
  $IR seq e:120 ret:250 wait:2 >/dev/null 2>&1
  if grep -q StartPlayback /home/dmontero/6g/exp2.log 2>/dev/null; then echo "StartPlayback FIRED after try $t"; break; fi
done
for k in 1 2 3 4 5; do $IR seq q:60 wait:1 e:60 wait:1 >/dev/null 2>&1; done
$IR raw quit >/dev/null 2>&1; sleep 2; pkill -9 -x qmus 2>/dev/null
echo "=== AUDIOPACE_START_MS=${AUDIOPACE_START_MS:-unset} resets: $(grep -c "software reset" "$OUT/qmus.err") ==="
sed -n "/PCTRACE summary/,\$p" /home/dmontero/6g/exp2.log
echo "=== AUDIOWAV ==="
grep -E "FIRST NON-ZERO|AUDIOWAV: wrote" "$OUT/qmus.err" | tail -2
python3 $ROOT/tools/ipod_classic/wavcheck.py "${AUDIOWAV:-/home/dmontero/6g/exp2.wav}" 2>/dev/null | grep -E "RMS|peak|non-zero|samples"
echo DONE_EXP2
