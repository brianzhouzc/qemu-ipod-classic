#!/bin/bash
# Deterministic navigation into a song, with evidence at every step.
#
# run_play.sh's navigation is not reproducible and that cost real measurements:
# two runs of the SAME pristine image disagreed on whether the library was there
# ("4 skladeb" vs "Zádná"), because its screenshot is taken while the database is
# still loading, and because the language the picker lands on differs per boot,
# which shifts every later menu.
#
# Fixed here:
#  - THE PICKER IS PINNED. Scrolling hard to the top selects the first entry
#    every time, so the locale is at least the SAME every run. (It is Japanese,
#    not English -- the list is not in the order one would guess.)
#  - The library is given time to load before anything is believed. The count
#    beside "Music" is only evidence once parsing has finished.
#  - Every SELECT uses the retrying form (the wheel drops ~5 of 7), and each step
#    leaves a screenshot, so a failed step is visible instead of silently
#    shifting the ones after it.
#  - The wake nudge is the pair e,q, in that order. A bare "-w e" scrolls down
#    one entry before every press, which opened Video instead of Music. The
#    obvious fix, "-w q,e", is WRONG at the top of a list: scrolling up from the
#    first entry is a no-op, so the pair nets +1 per retry, and a SELECT that
#    needed a few retries walked the highlight from Music down to Extras -- the
#    run after that fix landed in Calendars, not Songs. Down-then-up nets zero
#    at the top, which is where every list this script uses starts.
#
# Menu positions are used rather than menu text, so the language the picker ends
# on does not matter -- but the positions have to be the ones actually on screen,
# not the ones from memory. See step 4.
set +e
TAG=${1:-nav}
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
# See the note in run_play.sh: the emulator runs as qmus_$TAG, so a leftover one
# survives "pkill -x qemu-system-arm" and then answers the monitor on :4455 in
# place of the run being measured.
pkill -9 -f 'build/qmus[_]' 2>/dev/null
for _ in $(seq 1 30); do ss -ltn 2>/dev/null | grep -q ":4455 " || break; sleep 2; done

export AUDIOPACE=1 AUDIOWAV="$WAV"
for kv in "$@"; do export "$kv"; done

PA="nodefault=on,always=on,cap=8,window=300000,out=$LOG"
PA="$PA,pc=0x08150d84:StartPlayback,pc=0x22006118:dpump,pc=0x22006304:rendret"
PA="$PA,pc=0x0809a74c:mixer,pc=0x08016f08:copy16,pc=0x0816bf48:pcmout"
PA="$PA,pc=0x081e9e70:aacmath,pc=0x081e93d8:framepull"
PA="$PA,pc=0x081e93fc:pullpass,pc=0x081e9470:pullret"
PA="$PA${PCTRACE_EXTRA:+,$PCTRACE_EXTRA}"

setsid "$BIN" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=$IMG,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" \
  -gdb tcp::1234 \
  -plugin "$PLUG,$PA" >"$OUT/qmus_$TAG.out" 2>"$OUT/qmus_$TAG.err" &
echo "launched $TAG"
if [ -n "$PCMPROBE" ]; then
  ( timeout 280 gdb-multiarch -q -batch -x tools/session-dbg/${PROBE:-pcmdump_probe}.gdb \
      >"$OUT/gdb_$TAG.out" 2>"$OUT/gdb_$TAG.err" ) &
  sleep 3
fi
sleep 50
$IR wake e >/dev/null 2>&1
sleep 2

# 1. Language picker: go to the very top, then accept. Losing some of these does
# not matter -- 24 attempts overshoot any language list -- so they go in one
# connection rather than 24, which the earlier version spent over a minute on.
$IR seq q:120 q:120 q:120 q:120 q:120 q:120 q:120 q:120 q:120 q:120 q:120 q:120 \
        q:120 q:120 q:120 q:120 q:120 q:120 q:120 q:120 q:120 q:120 q:120 q:120 \
        wait:1 z >/dev/null 2>&1
sleep 1
$IR shot ${TAG}_1lang >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name lang >/dev/null 2>&1
sleep 3

# 2. Let the database finish loading before believing any count on screen.
sleep 25
$IR shot ${TAG}_2menu >/dev/null 2>&1

# 3. Into Music (top entry, already highlighted on a fresh main menu).
$IR press ret 250 -u -w e,q --region right -r 15 -t 3000 -s 5 --name music >/dev/null 2>&1
sleep 3
$IR shot ${TAG}_3music >/dev/null 2>&1

# 4. Down to Songs, then in. The Music submenu is NOT the four entries one
# expects: measured on screen it is Cover Flow, Playlists, Artists, Albums,
# Compilations, Songs, Genres, Composers, Audiobooks -- so Songs is the SIXTH
# entry, five scrolls from the top.
#
# The scrolls are VERIFIED, one at a time, for the same reason the selects are:
# a blind "seq e e e e e" landed only three of five and left the highlight on
# Albums, which then sent the rest of the run somewhere else entirely. A scroll
# moves the highlight, so a pixel diff is a reliable test that it happened.
for i in 1 2 3 4 5; do
  $IR press e 120 -u --region left -r 10 -t 800 -s 3 --name scroll$i >/dev/null 2>&1
done
$IR shot ${TAG}_4songsel >/dev/null 2>&1
$IR press ret 250 -u -w e,q --region right -r 15 -t 3000 -s 5 --name songs >/dev/null 2>&1
sleep 4
$IR shot ${TAG}_5songlist >/dev/null 2>&1

# 5. First song in the list.
$IR press ret 250 -u -w e,q --region right -r 15 -t 3000 -s 5 --name play >/dev/null 2>&1
sleep 8
$IR shot ${TAG}_6nowplaying >/dev/null 2>&1
sleep 20
$IR shot ${TAG}_7playing >/dev/null 2>&1
sleep 20
$IR shot ${TAG}_8playing >/dev/null 2>&1

exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 3 && exec 3<&-
sleep 2
echo "=== [$TAG] summary ==="
sed -n '/PCTRACE summary/,$p' "$LOG" | head -16
echo "resets=$(grep -ac 'software reset' "$OUT/qmus_$TAG.err")"
echo "wav=$(stat -c%s "$WAV" 2>/dev/null || echo 0) bytes"
echo "DONE $TAG"
