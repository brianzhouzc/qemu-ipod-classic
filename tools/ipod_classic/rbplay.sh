#!/bin/sh
# rbplay.sh <disk> <outdir> [wav]
# Boot Rockbox, enter Files (SELECT), play the first track (SELECT on the row-0
# MP3), and capture the Now Playing / WPS screen while the ARM codec (mpa.codec)
# decodes the MP3 and DMAs PCM to the I2S FIFO.  With a wav path, AUDIOWAV dumps
# that PCM to a WAV.  I2STRACE prints the TX start/stop and the pacing cadence.
#
#   env AUDIOPACE=1 sh rbplay.sh <disk> <out> <wav>   # gate DMA to sample rate
set -e
root=$(cd "$(dirname "$0")/../.." && pwd)
qemu="$root/build/qemu-system-arm"
disk=${1:-$HOME/6g/Erbmusic.img}
out=${2:-$HOME/6g/rbplay}
wav=${3:-$out/play.wav}
bin="$root/assets/rockbox/build-full/rockbox.bin"
mkdir -p "$out"; rm -f "$out"/*.ppm "$out"/*.png "$out"/qemu.err "$wav"
pkill -9 -x qemu-system-arm 2>/dev/null || true
sleep 1
{
    sleep 16; echo "screendump $out/0-menu.ppm"
    sleep 2;  echo "sendkey ret 150"        # SELECT -> enter Files
    sleep 3;  echo "screendump $out/1-files.ppm"
    sleep 1;  echo "sendkey ret 150"        # SELECT -> play row-0 MP3
    sleep 6;  echo "screendump $out/2-wps.ppm"
    sleep 8;  echo "screendump $out/3-wps.ppm"
    sleep 8;  echo "screendump $out/4-wps.ppm"
    sleep 6;  echo "screendump $out/5-wps.ppm"
    sleep 1;  echo quit
} | AUDIOWAV="$wav" I2STRACE=1 "$qemu" -M iPod-Classic \
        -kernel "$bin" \
        -drive "if=ide,format=raw,file=$disk,file.locking=off" \
        -snapshot \
        -display none -serial null -monitor stdio \
        >/dev/null 2>"$out/qemu.err" || true
for p in "$out"/*.ppm; do
    python3 "$root/tools/ipod_classic/ppm2png.py" "$p" "${p%.ppm}.png" || true
done
echo "=== I2S / TX start ==="
grep -E "TXCOM|NON-ZERO|drain|AUDIOWAV" "$out/qemu.err" | head -40 || true
echo "=== WAV ==="
ls -la "$wav" 2>/dev/null || echo "no wav"
echo "done: $out"
