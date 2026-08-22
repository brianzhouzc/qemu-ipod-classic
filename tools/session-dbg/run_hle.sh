#!/bin/bash
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
OUT=/home/dmontero/6g/remote; mkdir -p $OUT
export IREMOTE_OUT=$OUT
cd $ROOT; IR="python3 tools/ipod_classic/iremote.py"
LOG=/home/dmontero/6g/hle.log; rm -f $LOG
WAV=/home/dmontero/6g/hle.wav; rm -f $WAV
ERR=$OUT/qhle.err; rm -f $ERR
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
pkill -9 -x qhle 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qhle"
export AUDIOPACE=1 AUDIOWAV=$WAV
export HLE_AUDIO_DATAPATH=1 HLE_AUDIO_DISK=/home/dmontero/6g/Emulti.img HLE_AUDIO_TRACE=1
[ -n "$FTRACK" ] && export HLE_AUDIO_TRACK=$FTRACK
[ -n "$LIVE" ] && export AUDIOLIVE=1
[ -n "$SUST" ] && export HLE_AUDIO_SUSTAIN=1
PA="nodefault=on,always=on,cap=8,window=4000,out=$LOG"
PA="$PA,pc=0x08150d84:StartPlayback,pc=0x0808170c:engine,pc=0x22005e48:srcpoll,pc=0x08016eb4:copy32,pc=0x08016e44:copyfn,pc=0x08016ecc:copyskip,pc=0x081e9e70:innerdec"
setsid "$ROOT/build/qhle" -M iPod-Classic -kernel "$OSOS" -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" -snapshot -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234 -plugin "$PLUG,$PA" >$OUT/qhle.out 2>$ERR &
echo "launched pid $!"
sleep 52
$IR wake e >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name L1 >/dev/null 2>&1
CONF=0
for i in $(seq 1 15); do
  # over-scroll to the bottom item (Shuffle Songs / Nahodna hudba, last), then select
  $IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 ret:300 wait:3 z >/dev/null 2>&1
  if grep -aq 'StartPlayback' $LOG 2>/dev/null; then echo "PLAYBACK try $i"; CONF=1; break; fi
done
echo "CONF=$CONF"
echo "=== free-run ${FREE:-40}s (with NP screenshots) ==="; sleep 10; $IR shot np1 >/dev/null 2>&1; sleep 6; $IR shot np2 >/dev/null 2>&1; sleep $((${FREE:-40}-16))
exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 2 && exec 3<&-
sleep 2
echo '=== HLE stderr (id/decode/reads) ==='; grep -aE 'HLE_AUDIO: (armed|FAT scan|identified|decoded|read file|no PCM|cannot)' $ERR | head -20
echo '=== music-range reads (>=94142464) ==='; grep -aE 'HLE_AUDIO: dbg read' $ERR | awk -F'off=' '{print $2}' | awk '{if($1+0>=94142464 && $1+0<127000000) print}' | head -8
echo '=== counters ==='; grep -aE 'cpu[0-9]  (StartPlayback|engine|srcpoll|copy32|copyfn|copyskip|innerdec) ' $LOG | tail -14
echo "resets=$(grep -ac 'software reset' $ERR)"
python3 - "$WAV" << 'PYEOF'
import sys,wave,array
try:
    w=wave.open(sys.argv[1],'rb'); n=w.getnframes(); a=array.array('h'); a.frombytes(w.readframes(n))
    nz=sum(1 for x in a if x); pk=max((abs(x) for x in a), default=0)
    L=a[0::2]; start=next((i for i,x in enumerate(L) if abs(x)>500), -1)
    print('WAV rate=%d frames=%d nonzero=%.2f%% peak=%d loud_start=%.1fs'%(w.getframerate(),n,100.0*nz/max(1,len(a)),pk,start/44100.0 if start>=0 else -1))
except Exception as e: print('wav err',e)
PYEOF
pkill -9 -x qhle 2>/dev/null; echo DONE
