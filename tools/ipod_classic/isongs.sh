#!/bin/sh
# isongs.sh <disk>
# Boot the 6G retailOS and drive language -> main menu -> Music -> Songs, to
# show that a library whose iTunesDB hash58 is blanked (mkdisk6g4k.py) lists its
# track. Two flakes are fought here (wiki/13 s.8):
#   1. A COLD centre press is ignored -- the retailOS only honours SELECT while
#      the wheel is active. So every SELECT is preceded by wheel motion. When the
#      current row is the target (e.g. Music, the top row) that motion must NOT
#      move the highlight, so iremote.py's `press -u -w q` scrolls up (a top row
#      cannot move, menus do not wrap), presses centre, and stops the instant a
#      screendump pixel-diff shows the submenu. Where a scroll is needed anyway
#      (language list, Songs list) the SELECT is open-loop right after it, which
#      is reliable because the scroll just touched the wheel.
#   2. The panel sleeps after ~15 s idle and a sleeping guest that misses its
#      deadlines resets (SWRCON=0xaa5); so the waits are short and the panel is
#      woken before the first step.
# Screendumps: ~/6g/remote/S0*.png ; the song list is S04-songs.png.
set -e
DISK=$1
ROOT=/mnt/c/Desarrollo/qemu-ios
T=$ROOT/tools/ipod_classic
IR="python3 $T/iremote.py"

bash "$T/irun.sh" "$DISK"
sleep 50

# Wake the panel until it is lit (a slow boot or the ~15 s sleep leaves it
# black); a black baseline would make the next step's right-half diff read the
# black->lit relight as a false SELECT and drift the whole run.
$IR wake e
$IR shot S00-lang
# Language -> main menu. Retry wake(e, scrolls+touches) + SELECT until the RIGHT
# half changes: the "Welcome" panel is static under scrolling and only a real
# selection redraws it, so --region right tells a landed SELECT from a mere
# highlight move (a full-frame diff cannot, and drifts the whole run). Lands on
# whatever language is highlighted -- cosmetic, the title/artist are Latin.
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name S01-main
# Main -> Music. THE flaky SELECT. Retry wake(q holds the top row in place) +
# centre until the submenu replaces the menu.
$IR press ret 250 -u -w q -r 15 -t 1500 -s 5 --name S02-music
# Music submenu -> highlight Songs (Cover Flow=0 ... Songs=5): five steps down.
$IR seq e:120 wait:2 e:120 wait:2 e:120 wait:2 e:120 wait:2 e:120 wait:3 S03-songsel
# Songs -> the song list. q,e re-touches the wheel and stays on Songs, then
# SELECT; a final q re-lights the panel (it may have slept during the wait) and,
# on the one-track list, does not move the selection, so the shot is not black.
$IR seq q:120 wait:1 e:120 wait:1 ret:250 wait:5 q:120 wait:2 S04-songs

echo "done; song list is ~/6g/remote/S04-songs.png (resets: $(grep -c 'software reset' ~/6g/remote/qemu.err))"
# NOTE: three SELECT gates, each ~5/7-lossy (wiki/11 s.18.8) and retried here;
# a bad streak or a missed-deadline reset can still leave a step short. Re-run,
# or drive by hand with iremote.py -- how the verified ~/6g/song_listed/ capture
# was taken. The primitives (press -w / --region / wake) are the reliable part.
