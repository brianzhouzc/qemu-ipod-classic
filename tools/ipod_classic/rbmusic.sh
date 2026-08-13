#!/bin/sh
# rbmusic.sh [disk-image] -- play music on the emulated iPod Classic 6G with the
# sound coming OUT OF THE HOST SPEAKERS, in one command.
#
# Boots Rockbox (loaded straight into SDRAM with -kernel, the osos hand-off the
# machine reproduces) with the graphical window up and audio wired to the host:
#   AUDIOPACE=1  drains the audio DMA at the real sample rate (48 kHz for the
#                sample tracks) so playback runs at the right tempo -- WITHOUT it
#                the DMA dumps the whole buffer at once and there is no music.
#   AUDIOLIVE=1  besides the (optional) WAV capture, feeds the decoded PCM to a
#                QEMU SWVoiceOut, so it comes out of the speakers live.
#   -audio pa    the PulseAudio backend. Under WSLg this reaches the Windows
#                speakers (PULSE_SERVER=/mnt/wslg/PulseServer -> RDPSink). On a
#                bare Linux desktop 'pa' targets the local PulseAudio/Pipewire.
#
# RUN IT (from WSL -- the binary is a Linux ELF, WSLg gives it window + sound):
#   sh tools/ipod_classic/rbmusic.sh
#   sh tools/ipod_classic/rbmusic.sh ~/6g/Erbmusic.img      # explicit disk
#   AUDIOWAV=/tmp/out.wav sh tools/ipod_classic/rbmusic.sh  # also capture a WAV
#   RBPERSIST=1 sh tools/ipod_classic/rbmusic.sh            # save disk writes
#
# KEYS (focus the QEMU window; the click wheel and buttons are mapped to these
# host keys -- see hw/arm/ipod_classic/ipod_classic_wheel.c):
#   e / q     click wheel forward / back  (scroll the highlight down / up)
#   Enter     SELECT (center button)      -- open the highlighted item / play
#   w         MENU  (back / up a level)
#   s         PLAY / PAUSE
#   d / a     next / previous track
#
# TO PLAY A SONG: let it boot to the main menu (~15 s), then
#   Enter (open Files or Music) -> e/q to a track -> Enter to play.
# The Now Playing screen appears and the song comes out of the speakers.
#
# NOTES
#   * A no-audio host still boots and plays silently -- pa just fails to open.
#   * Playback smoothness depends on the host sustaining real-time MP3 decode on
#     the emulated ARM; a fast/idle host plays clean, a loaded one may glitch.
#   * By default the disk is opened read-only-ish (-snapshot): the emulator's
#     writes are discarded so an emulator crash cannot corrupt your image. Set
#     RBPERSIST=1 to let Rockbox save settings/database back to the disk.
set -e

root=$(cd "$(dirname "$0")/../.." && pwd)
qemu="$root/build/qemu-system-arm"
bin="$root/assets/rockbox/build-full/rockbox.bin"
[ -f "$bin" ] || bin="$root/assets/rockbox/build-rb/rockbox.bin"
disk="${1:-$HOME/6g/Erbmusic.img}"

[ -x "$qemu" ] || { echo "no $qemu -- build it first:"                       >&2
    echo "  wsl -d Ubuntu-24.04 -e bash -c 'cd $root/build && ninja qemu-system-arm'" >&2
    exit 1; }
[ -f "$bin" ]  || { echo "no rockbox.bin ($bin) -- build the Rockbox app"     >&2; exit 1; }
[ -f "$disk" ] || { echo "no disk image $disk (Rockbox + tracks)"             >&2
    echo "  see tools/ipod_classic/rockbox.sh header for how to build one"    >&2; exit 1; }

# Persist disk writes only if asked; otherwise -snapshot protects the image.
snap="-snapshot"
[ -n "$RBPERSIST" ] && snap=""

echo "iPod Classic 6G + Rockbox -- audio to the host speakers (PulseAudio)."
echo "keys:  e/q wheel   Enter select   w menu   s play/pause   d/a next/prev"
echo "play:  boot (~15s) -> Enter -> e/q to a track -> Enter.  Ctrl-C here to quit."

exec env AUDIOPACE=1 AUDIOLIVE=1 ${AUDIOWAV:+AUDIOWAV="$AUDIOWAV"} \
    "$qemu" -M iPod-Classic \
    -kernel "$bin" \
    -drive "if=ide,format=raw,file=$disk,file.locking=off" \
    $snap \
    -audio pa \
    -display sdl
