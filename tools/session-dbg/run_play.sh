#!/bin/bash
# Drive the retailOS to actually PLAY a song, and measure whether audio comes out.
#
# Navigation notes learned the hard way:
#  - A fresh boot (-snapshot keeps nothing) starts in the LANGUAGE picker, not the
#    main menu. It has to be dismissed first or every later keypress is choosing a
#    language instead of a song.
#  - The retailOS clickwheel DROPS roughly five of every seven SELECT presses
#    (documented in iremote.py), so every SELECT here uses the retrying form.
#    A single press looks exactly like a dead button.
#  - SHA1_OFF=1 keeps the library readable (the engine, once modelled, rejects the
#    image's iTunesDB hash) at the cost of the ~46 s panic-reset coming back, so
#    this has to reach playback quickly.
#
# Measures: the native render counters plus the I2S WAV, so "did music come out"
# is answered by RMS rather than by looking at the screen.
set +e
TAG=${1:-play}
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
for _ in $(seq 1 30); do ss -ltn 2>/dev/null | grep -q ":4455 " || break; sleep 2; done

export AUDIOPACE=1 AUDIOWAV="$WAV"
for kv in "$@"; do export "$kv"; done

PA="nodefault=on,always=on,cap=8,window=300000,out=$LOG"
PA="$PA,pc=0x08150d84:StartPlayback,pc=0x22006118:dpump,pc=0x0808170c:engine"
PA="$PA,pc=0x22006304:rendret,pc=0x08082100:ctxbuild,pc=0x0809a74c:mixer"
PA="$PA,pc=0x08016e44:copyent,pc=0x08016f08:copy16,pc=0x08016eb4:copy32"
PA="$PA,pc=0x0816bf48:pcmout,pc=0x081e9e70:aacmath"
# The production-credit gate: the frame-pull 0x081e93d8 bails when
# [[[reader+4]+0x30]+0x2b8] is 0, so the decoder stops after one frame.
# These are the only four sites in the image that write that field.
PA="$PA,pc=0x081cc1f4:cred1,pc=0x081cc7a4:cred2,pc=0x081d7548:cred3,pc=0x081e360c:cred4"
PA="$PA,pc=0x081c0fd4:credread,pc=0x081e93d8:framepull,pc=0x081c1508:fpull2"
# Is the PLAY-time source the same buffer as the boot-time one? 0x08016edc is a
# branch target (TB head, so the register read is reliable): r0 = source pointer,
# r4 = descriptor. If these differ from 0x08889098/0x0bb12d54, the memw watch
# below has been aimed at the wrong region all along.
# Who CALLS the frame pull, and what state does it see? lr identifies the caller;
# 0x081e93d8 is a function entry so the register read is reliable there.
PA="$PA,regpc=0x081e93d8:pullregs,regpc=0x081c0fd4:credregs,regcap=30"
# Does the pull get PAST the credit gate? 0x081e93fc is the first instruction
# after "beq exit"; 0x081e9470 is the early-exit landing. Exactly one of them
# runs per pull, so this says whether the credit was zero.
PA="$PA,pc=0x081e93fc:pullpass,pc=0x081e9470:pullbail,pc=0x081e9bc0:vslot15"
# The audio-engine interrupt chain: 0x08017364 ARMS it (writes bit 2 to SM1+0xa8c),
# 0x08017378 is the VIC line-18 handler, 0x080173cc is where it signals the event
# 0x22008c58 that the render pump waits on. Before the context-build fix the arming
# site was never reached (the render took the other path).
PA="$PA,pc=0x08017364:sm1arm,pc=0x08017378:credithdlr,pc=0x080173cc:creditsig"
PA="$PA,pc=0x080820e4:armcall,pc=0x08082020:rearly,pc=0x08081734:rfull"
PA="$PA,regpc=0x08016edc:srcregs,regcap=30"
# CORRECTED WINDOW: the play-time copy reads from 0x0888a2xx-0x0888a6xx (measured
# live), not from the boot-time 0x08889098. The earlier watch missed the real
# buffer entirely, which is why it only ever saw memset.
# The decoder work-queue object (obj = [reader+4], measured 0x0ba90c50): +0x3c is
# the producer index, +0x40 the consumer index, +0x44 the sample count handed to
# the codec. Measured live: prod=3 cons=2 depth=1 but [obj+0x44]=0 and
# [reader+8]=0, so the codec is invoked asking for ZERO samples. Watch who writes
# these fields -- that is the producer that should be filling the count.
# Does the guest READ THE AUDIO FILE during playback? If StreamCache/MeCCAIOTask
# never pull compressed data off the disk, the decoder has no input and every
# downstream measurement is moot. Watch the ATA register window (0x38700000).
# Watch the REAL play-time mixer source ring while the codec count is forced
# non-zero. If decoded PCM lands here, the count was the last missing piece.
# The DECODER OUTPUT buffers, from the live reader dump at the codec entry:
# [reader+0x0c]=0x0ba77e70 [reader+0x10]=0x0ba77e74 [reader+0x14]=0x0ba77ef0.
# The mixer reads a different region entirely (0x0888axxx), so if PCM lands here
# the decoder works and only the hand-off to the mixer is missing.
PA="$PA,memw=0x0ba77e00-0x0ba78200,memcap=40"

setsid "$BIN" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=$IMG,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" \
  -gdb tcp::1234 \
  -plugin "$PLUG,$PA" >"$OUT/qmus_$TAG.out" 2>"$OUT/qmus_$TAG.err" &
echo "launched $TAG"
if [ -n "$PCMPROBE" ]; then
  ( timeout 200 gdb-multiarch -q -batch -x tools/session-dbg/${PROBE:-pcmdump_probe}.gdb \
      >"$OUT/gdb_$TAG.out" 2>"$OUT/gdb_$TAG.err" ) &
  sleep 3
fi
sleep 50
$IR wake e >/dev/null 2>&1
sleep 2

# 1. Dismiss the language picker (accept whatever is highlighted).
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name lang >/dev/null 2>&1
sleep 3
$IR shot ${TAG}_menu >/dev/null 2>&1

# 2. Main menu -> last entry (Shuffle Songs) -> SELECT, retrying.
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:2 z >/dev/null 2>&1
$IR shot ${TAG}_shuffle >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name go >/dev/null 2>&1
sleep 6
$IR shot ${TAG}_play1 >/dev/null 2>&1

# 3. Let it run, then look again.
sleep 15
$IR shot ${TAG}_play2 >/dev/null 2>&1
sleep 15
$IR shot ${TAG}_play3 >/dev/null 2>&1

echo "=== [$TAG] counters ==="
sed -n '/PCTRACE summary/,$p' "$LOG" | head -20
exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 3 && exec 3<&-
sleep 2
echo "=== [$TAG] summary ==="
sed -n '/PCTRACE summary/,$p' "$LOG" | head -20
echo "resets=$(grep -ac 'software reset' "$OUT/qmus_$TAG.err")"
echo "wav=$(stat -c%s "$WAV" 2>/dev/null || echo 0) bytes"
echo "DONE $TAG"
