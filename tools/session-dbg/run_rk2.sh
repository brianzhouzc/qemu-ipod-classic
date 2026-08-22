#!/bin/bash
# Session-11 v2: instrument t25's path after the ring wake. Where does the
# woken engine task go -- recv return, data-pump, bail, or engine call?
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
export IREMOTE_OUT=$OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/rk2.log; rm -f $LOG
ERR=$OUT/qmus.err
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1
export HLE_RENDER_KICK="${RK:-1}"
[ -n "$RKONCE" ] && export HLE_RENDER_KICK_ONCE=1
PA="nodefault=on,always=on,cap=4,window=4000,out=$LOG"
PA="$PA,pc=0x08150d84:StartPlayback"
PA="$PA,pc=0x22006814:recv,pc=0x22006824:recvret"
PA="$PA,pc=0x22006118:dpump,pc=0x22006194:dppass,pc=0x22006300:dpeng,pc=0x22006188:dpbail,pc=0x22006360:dpalt"
PA="$PA,pc=0x0808170c:engine,pc=0x0809a74c:mixer,pc=0x08016eb4:copy32,pc=0x08123970:h12"
PA="$PA,sendex=0x22004138:3:0x6:ex6"
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234 \
  -plugin "$PLUG,$PA" >$OUT/qmus.out 2>$ERR &
echo "launched pid $! (RK=$HLE_RENDER_KICK ONCE=${RKONCE:-0})"
sleep 52
$IR wake e >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name L1 >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:2 L2 >/dev/null 2>&1
CONF=0
for i in 1 2 3 4 5 6 7 8; do
  $IR seq e:120 wait:1 ret:250 wait:2 z >/dev/null 2>&1
  if grep -aqE 'cpu[0-9]  (h12|ex6|recv) ' $LOG 2>/dev/null; then echo "PROGRESS try $i"; CONF=1; break; fi
done
echo "CONF=$CONF"
echo "=== free-run ${FREE:-16}s ==="; sleep "${FREE:-16}"
echo "=== LIVE snapshot (t25/engobj/ring) while kick active ==="
timeout 40 gdb-multiarch -q -batch -x ~/dbg/rk_probe.py 2>&1 | grep -avE 'auto-load|safe path|No executable|Reading|warning: A|Remote deb|GNU gdb|Copyright|License|free soft|NO WARRANTY|show copy|Type .show|For bug|Find the|manual|Detaching|Ending remote|^\[Inferior|determining exec|configuration file|disable this|information about'
exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 2 && exec 3<&-
sleep 2
echo "=== rang_total=$(grep -ac 'rang render doorbell' $ERR) ==="
echo "=== PCTRACE summary ==="
sed -n '/PCTRACE summary/,$p' $LOG | head -40
echo "resets=$(grep -c 'software reset' $ERR)"
pkill -9 -x qmus 2>/dev/null
echo DONE
