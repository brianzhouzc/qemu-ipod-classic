#!/bin/bash
# Does the audio task keep running when NOBODY touches the wheel?
#
# The render pump is dispatched in bursts 17-21 s apart (wiki/19 s11.50), and
# those instants -- 80.9 s, 98.4 s, 119.6 s -- fall inside run_play.sh's own
# navigation script, which sleeps 6, 15 and 15 s between key presses. That is
# suspicious enough to test directly: if the task is really self-clocked off the
# audio engine it will keep going with the harness silent, and if it is only
# being woken because the UI pokes it, the bursts stop dead.
#
# So: reach playback exactly as usual, then issue NO monitor traffic at all --
# not even a screenshot, since a screendump is itself a monitor round trip -- for
# two full minutes, and read the counters afterwards.
set +e
TAG=${1:-quiet}
shift 2>/dev/null
ROOT=$HOME/qemu-ios
OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
IMG=${IMG:-$HOME/6g/Emulti.img}
OUT=$HOME/6g/remote; mkdir -p "$OUT"
export IREMOTE_OUT=$OUT
cd "$ROOT" || exit 1
IR="python3 tools/ipod_classic/iremote.py"
LOG=$HOME/6g/meas_$TAG.log; rm -f "$LOG"
WAV=$HOME/6g/$TAG.wav; rm -f "$WAV"
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
BIN=$ROOT/build/qmus_$TAG
cp "$ROOT/build/qemu-system-arm" "$BIN"
pkill -9 -f 'build/qmus[_]' 2>/dev/null
for _ in $(seq 1 30); do ss -ltn 2>/dev/null | grep -q ":4455 " || break; sleep 2; done

export AUDIOPACE=1 AUDIOWAV="$WAV"
for kv in "$@"; do export "$kv"; done

PA="nodefault=on,always=on,cap=8,window=600000,out=$LOG"
PA="$PA,pc=0x08150d84:StartPlayback,pc=0x22006118:dpump,pc=0x22006304:rendret"
PA="$PA,pc=0x0809a74c:mixer,pc=0x08016f08:copy16,pc=0x0816bf48:pcmout"
PA="$PA,pc=0x081e9e70:aacmath,pc=0x080173cc:creditsig"
PA="$PA${PCTRACE_EXTRA:+,$PCTRACE_EXTRA}"

setsid "$BIN" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=$IMG,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" \
  -plugin "$PLUG,$PA" >"$OUT/qmus_$TAG.out" 2>"$OUT/qmus_$TAG.err" &
echo "launched $TAG"
sleep 50
$IR wake e >/dev/null 2>&1
sleep 2
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name qlang >/dev/null 2>&1
sleep 3
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:2 z >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name qgo >/dev/null 2>&1
sleep 5

echo "=== [$TAG] counters at the START of the quiet window ==="
sed -n '/PCTRACE summary/,$p' "$LOG" | grep -aE 'dpump|copy16|aacmath|creditsig|pcmout'

# ---- absolute silence from here: no monitor traffic whatsoever ----
sleep 120

exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 3 && exec 3<&-
sleep 2
echo "=== [$TAG] counters AFTER 120 s untouched ==="
sed -n '/PCTRACE summary/,$p' "$LOG" | grep -aE 'dpump|copy16|aacmath|creditsig|pcmout'
echo "wav=$(stat -c%s "$WAV" 2>/dev/null || echo 0) bytes"
echo "DONE $TAG"
