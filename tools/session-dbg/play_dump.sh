#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
PLUG=$ROOT/build/contrib/plugins/libpctrace.so; OUT=/home/dmontero/6g/remote
cd $ROOT; IR='python3 tools/ipod_classic/iremote.py'
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
PA="nodefault=on,always=on,cap=3,pc=0x08150d84:StartPlayback,pc=0x0808170c:audio_engine,pc=0x08016eb4:copy32REAL,pc=0x080a3c00:recv3c00,out=/home/dmontero/6g/pd.log"
export AUDIOPACE=1
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -plugin "$PLUG,$PA" >$OUT/qmus.err 2>&1 &
sleep 28
$IR wake e 3000 >/dev/null 2>&1
pgrep -x qmus >/dev/null || { echo DIED; exit 1; }
$IR seq w:120 e:120 e:120 e:120 e:120 e:120 e:120 PD >/dev/null 2>&1
$IR press ret 200 -u -w e -r 8 -t 1500 -s 1 --name PDS >/dev/null 2>&1
sleep 1
if [ "$(grep -c 'StartPlayback @' /home/dmontero/6g/pd.log)" -ge 1 ]; then
  echo FIRED
  $IR raw 'xp/1xb 0x22008ca4' > /home/dmontero/6g/codectype.txt 2>&1
  $IR raw 'xp/160xw 0x22008c4c' > /home/dmontero/6g/audiostate.txt 2>&1
  $IR raw 'xp/728xw 0x0897ff70' > /home/dmontero/6g/tcb.txt 2>&1
  $IR raw 'xp/2xw 0x2200ac64' > /home/dmontero/6g/curtcb.txt 2>&1
else
  echo NOFIRE
fi
sleep 2
pgrep -x qmus >/dev/null && $IR raw quit >/dev/null 2>&1; sleep 2; pkill -9 -x qmus 2>/dev/null
echo '=== SUMMARY ==='; sed -n '/PCTRACE summary/,$p' /home/dmontero/6g/pd.log 2>/dev/null
echo '=== codectype ==='; cat /home/dmontero/6g/codectype.txt 2>/dev/null
