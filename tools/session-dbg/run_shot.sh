#!/bin/bash
# Navigate to a song, press play, and capture what the retailOS actually draws --
# visual evidence of the native Now Playing screen now that the render loop runs
# (the SM1 ring-offset fix, wiki/19 s11.26). No HLE overlay: this is the guest's
# own framebuffer.
set +e
TAG=${1:-shot}
shift 2>/dev/null
ROOT=$HOME/qemu-ios
OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
IMG=$HOME/6g/Emulti.img
OUT=$HOME/6g/remote; mkdir -p "$OUT"
export IREMOTE_OUT=$OUT
cd "$ROOT" || exit 1
IR="python3 tools/ipod_classic/iremote.py"
LOG=$HOME/6g/meas_$TAG.log; rm -f "$LOG"
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
BIN=$ROOT/build/qmus_$TAG
cp "$ROOT/build/qemu-system-arm" "$BIN"

export AUDIOPACE=1
for kv in "$@"; do export "$kv"; done

PA="nodefault=on,always=on,cap=8,window=300000,out=$LOG"
PA="$PA,pc=0x22006118:dpump,pc=0x0808170c:engine,pc=0x22006304:rendret"
PA="$PA,pc=0x08016eb4:copy32,pc=0x08085ec4:memcpyret,pc=0x08016e44:copyent"

setsid "$BIN" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=$IMG,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" \
  -plugin "$PLUG,$PA" >"$OUT/qmus_$TAG.out" 2>"$OUT/qmus_$TAG.err" &
echo "launched $TAG pid $!"
sleep 52
$IR wake e >/dev/null 2>&1
$IR shot 00_boot >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name L1 >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:2 L2 >/dev/null 2>&1
$IR shot 01_menu >/dev/null 2>&1
for i in 1 2 3 4 5 6 7 8; do
  $IR seq e:120 wait:1 ret:250 wait:2 z >/dev/null 2>&1
  if grep -aqE 'cpu[0-9]  (engine|dpump) ' "$LOG" 2>/dev/null; then echo "PLAY try $i"; break; fi
done
sleep 4;  $IR shot 02_play_t4  >/dev/null 2>&1
sleep 8;  $IR shot 03_play_t12 >/dev/null 2>&1
sleep 10; $IR shot 04_play_t22 >/dev/null 2>&1
echo "=== [$TAG] counters ==="
sed -n '/PCTRACE summary/,$p' "$LOG" | head -12
exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 2 && exec 3<&-
sleep 2
ls -la "$OUT"/*.ppm 2>/dev/null | tail -6
echo "resets=$(grep -c 'software reset' "$OUT/qmus_$TAG.err")"
echo "DONE $TAG"
