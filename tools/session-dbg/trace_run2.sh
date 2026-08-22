#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios
OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
cd $ROOT
IR="python3 tools/ipod_classic/iremote.py"
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
PA="nodefault=on,always=on,cap=6"
PA="$PA,pc=0x08150d84:StartPlayback"
PA="$PA,pc=0x0808170c:audio_engine"
PA="$PA,pc=0x0809a74c:mixer"
PA="$PA,pc=0x08016e44:copyPCM"
PA="$PA,pc=0x08016eb4:copy32_REALPCM"
PA="$PA,pc=0x080c8968:decode_RPC"
PA="$PA,pc=0x080833d0:worker_body"
PA="$PA,pc=0x080339d8:KS_receive"
PA="$PA,pc=0x08033af8:KS_signal"
PA="$PA,pc=0x080cc0e4:exchange_send"
PA="$PA,out=/home/dmontero/6g/pctrace2.log"
export AUDIOPACE=1
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" \
  -plugin "$PLUG,$PA" >"$OUT/qmus.out" 2>"$OUT/qmus.err" &
echo "launched, booting..."
sleep 48
$IR wake e 3000 >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 8 -t 3000 -s 3 --name U01 2>&1 | tail -1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 U02 2>&1 | tail -1
echo "SELECT + play 18s"
for t in 1 2 3; do $IR seq e:120 ret:220 wait:1 >/dev/null 2>&1; done
for k in 1 2 3 4 5; do $IR seq q:60 wait:1 e:60 wait:1 >/dev/null 2>&1; done
$IR raw quit >/dev/null 2>&1
sleep 2
pkill -9 -x qmus 2>/dev/null
echo "=== resets: $(grep -c "software reset" "$OUT/qmus.err") ==="
echo "=== pctrace2 summary ==="
sed -n "/PCTRACE summary/,\$p" /home/dmontero/6g/pctrace2.log
echo "DONE_TRACE_RUN2"
