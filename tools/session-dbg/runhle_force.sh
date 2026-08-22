#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/hletest.log; rm -f $LOG
WAV=/home/dmontero/6g/hle_out.wav; rm -f $WAV
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qmus 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmus"
export AUDIOPACE=1 HLE_AUDIO_KICK=1 HLE_AUDIO_KICK_TRACE=1 HLE_AUDIO_KICK_FORCE=1 AUDIOWAV=$WAV
PA="nodefault=on,always=on,cap=3,out=$LOG,pc=0x08150d84:StartPlayback"
PA="$PA,pc=0x08123dcc:disp,pc=0x08123970:h12,pc=0x08123bfc:dwork"
PA="$PA,pc=0x0809a74c:mixer,pc=0x08016eb4:copy32"
PA="$PA,sendex=0x22004138:3:0x6:ex6"
setsid "$ROOT/build/qmus" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234 \
  -plugin "$PLUG,$PA" >$OUT/qmus.out 2>$OUT/qmus.err &
echo "launched pid $!"
sleep 50
$IR wake e 3000 >/dev/null 2>&1
# language -> main menu (reliable, any language)
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 4 --name Pmain >/dev/null 2>&1
# scroll to the last main-menu item (Shuffle Songs); overscroll stays at bottom
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 Pscroll >/dev/null 2>&1
# reliable SELECT of Shuffle Songs: retry, net-zero q,e touch keeps highlight
$IR press ret 300 -u -w q,e --region right -r 5 -t 1800 -s 3 --name Psel >/dev/null 2>&1
echo "select diff done"
sleep 22
exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 2 && exec 3<&-
sleep 2
echo "=== HLE stderr (gate trajectory + kick) ==="; grep -a "HLE_AUDIO_KICK" $OUT/qmus.err | grep -aE "emitted|gate=0x[0-9a-f]*[1-9a-f]|armed|play start" | head -30
echo "--- (all gate samples, tail) ---"; grep -a "attempt" $OUT/qmus.err | tail -6
echo "=== first-nonzero PCM ==="; grep -a "FIRST NON-ZERO" $OUT/qmus.err | head
echo "=== AUDIOWAV ==="; grep -a "AUDIOWAV: wrote" $OUT/qmus.err | tail -1
echo "=== pctrace counters ==="; sed -n "/PCTRACE summary/,\$p" $LOG | head -20
echo "=== ex6 count ==="; grep -a "ex6" $LOG | grep -a "hits=" | head
echo "=== wav nonzero/stereo ==="
python3 - "$WAV" << "PYEOF"
import sys,wave,array
try:
    w=wave.open(sys.argv[1],"rb"); n=w.getnframes(); a=array.array("h"); a.frombytes(w.readframes(n))
    L=a[0::2]; R=a[1::2]; nz=sum(1 for x in a if x); diff=sum(1 for i in range(min(len(L),len(R))) if L[i]!=R[i])
    print("frames=%d nonzero=%d LneR=%d"%(n,nz,diff))
except Exception as e: print("err",e)
PYEOF
echo "resets=$(grep -c "software reset" $OUT/qmus.err)"
pkill -9 -x qmus 2>/dev/null
echo DONE
