#!/bin/sh
# iplay.sh <disk> [wavpath]
# Boot the 6G retailOS, navigate language -> Music -> Songs -> SELECT the first
# song -> Now Playing, and capture a burst of Now Playing frames so the progress
# bar / track info can be inspected and (if paced) watched to advance. If a WAV
# path is given, AUDIOWAV is set so the I2S dumps captured PCM there.
#
# This extends isongs.sh (which stops at the song list) with the final SELECT
# and a timed burst of screendumps. SELECT is ~5/7-lossy so the song SELECT is
# retried until the frame changes.
#
# Screendumps: ~/6g/remote/P0*.png. The Now Playing burst is P10..P16.
set -e
DISK=$1
WAV=$2
ROOT=/mnt/c/Desarrollo/qemu-ios
T=$ROOT/tools/ipod_classic
IR="python3 $T/iremote.py"

# irun.sh runs qmus via setsid, which inherits this environment, so exporting
# AUDIOWAV/I2STRACE here reaches the emulator.
export AUDIOWAV="$WAV"
export I2STRACE="${I2STRACE:-}"
bash "$T/irun.sh" "$DISK"
sleep 50

$IR wake e
$IR shot P00-lang
# Language -> main menu (land on whatever language is highlighted).
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name P01-main
# Main -> Music.
$IR press ret 250 -u -w q -r 15 -t 1500 -s 5 --name P02-music
# Music submenu -> Songs (Cover Flow=0 ... Songs=5): five steps down.
$IR seq e:120 wait:2 e:120 wait:2 e:120 wait:2 e:120 wait:2 e:120 wait:3 P03-songsel
# Songs -> the song list (closed loop, q,e nets zero on Skladby).
$IR press ret 250 -u -w q,e --region right -r 20 -t 1500 -s 4 --name P04-songs
# DIAGNOSTIC: is input still alive AT the song list? Scroll down one and shoot.
# If the highlight moves, the wheel works here; if not, entering the list froze
# input (candidate: the firmware pre-buffers the song via the unmodelled DSP).
$IR seq e:120 wait:2 P05-scrolltest
$IR seq e:120 wait:2 P06-scrolltest2
# DISCRIMINATOR: does an ACTIVE-WHEEL button work on the list? Touch (q) then
# MENU. If this navigates back to the Music submenu, BUTTONS work on the list
# and the song SELECT failing to play is a play-path problem; if MENU also does
# nothing, buttons are not honoured on the list at all.
$IR seq q:120 w:250 wait:2 P07-touchmenu
# and back into the list for the SELECT test (scroll to Songs again is costly;
# just try SELECT from wherever we are).
# SELECT the highlighted song, FAST (tight scroll->SELECT, short settle), and
# grab a frame right after each try before the device can idle.
$IR seq q:120 ret:150 wait:2 P10-nowplaying
$IR seq e:120 ret:150 wait:2 P11-np
$IR seq q:120 ret:150 wait:2 P12-np
$IR seq s:120 wait:2 P13-np
$IR seq q:120 ret:150 wait:3 P14-np

echo "done; Now Playing frames ~/6g/remote/P1*.png (resets: $(grep -c 'software reset' ~/6g/remote/qemu.err 2>/dev/null))"
