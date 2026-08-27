#!/bin/bash
# Assault #13: measure the NATIVE render pump in the CLEAN baseline (no HLE_NP,
# no SUSTAIN, no gdb hooks) -- the configuration the firmware actually sees.
#
# Chain under test (decoded from iram.dis/osos.dis this session):
#   0x22006118 dpump    : the render pump function
#   0x22006300 -> 0x0808170c engine render        (engine)
#   0x22006304 rendret  : does the render RETURN to the pump?
#   0x22006324 pumploop : the retry-loop head
#   0x0808af58 pumppred : predicate = wait(event 0x22008c58, 500ms)
#   0x22006310 pumpretry: sleep-and-retry (2 strikes then teardown)
#   0x08017378 credithdlr : IRQ-18 handler that SIGNALS event 0x22008c58
#   0x080173cc creditsig  : the signal call inside it
# Question: in the clean baseline, does the render return and the pump poll?
# And does the credit handler ever run (it needs IRQ 18, which our VIC map --
# inherited from Rockbox, which does not use SM1 -- never wires).
set +e
TAG=${1:-pump}
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
pkill -9 -f "qmus_$TAG" 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$BIN"

# Repeated runs kept colliding on the monitor port and silently producing no
# log, so wait for the monitor port to actually be free before launching.
for _ in $(seq 1 30); do
  ss -ltn 2>/dev/null | grep -q ":4455 " || break
  sleep 2
done

# CLEAN baseline: only AUDIOPACE, exactly like run_icount.sh off.
export AUDIOPACE=1
for kv in "$@"; do export "$kv"; done

PA="nodefault=on,always=on,cap=8,window=8000,out=$LOG"
PA="$PA,pc=0x08150d84:StartPlayback,pc=0x22006118:dpump,pc=0x0808170c:engine"
PA="$PA,pc=0x22006304:rendret,pc=0x22006324:pumploop,pc=0x22006310:pumpretry"
PA="$PA,pc=0x0808af58:pumppred,pc=0x08017378:credithdlr,pc=0x080173cc:creditsig"
PA="$PA,pc=0x0809a74c:mixer,pc=0x08016e44:copyent,pc=0x08016eb4:copy32"
PA="$PA,pc=0x22002fe0:motorpred,pc=0x22002ff4:motorpump,pc=0x220067e4:jobrepost,pc=0x22006188:pumpabort"
# NOTE: the 0x08123xxx cluster is the iMAImageCacheThread (album art), NOT audio.
# The real question is who fills the mixer source ring at 0x08889098.
# The engine-context build: 0x080820f0 reads sub-block 4 state; if != 0 it SKIPS
# 0x08082100-0x080821a0 (buffer copy, descriptor alloc, chain append, addr xlate).
PA="$PA,pc=0x080820f0:statetest,pc=0x08082100:ctxbuild,pc=0x080821a8:ctxskip"
PA="$PA,pc=0x080b3104:addrxlate,pc=0x08083144:xlate_ok,pc=0x0808315c:xlate_fail"
PA="$PA,pc=0x081e9e70:aacmath,pc=0x08016e88:gatechk,pc=0x08016e94:copygo"
# The REACHABLE store: from the live call site the 32-bit path is structurally
# unreachable ([r4]!=0 always diverts at 0x08016e84), so real PCM goes through
# the halfword path 0x08016edc gated by [descr+21]==1, storing at 0x08016f08.
# The real PCM OUTPUT path: 0x0816bf48 contains the firmware's own debug dump
# to "MeCCA_PCM_Output.wav" (flag struct at 0x08948f24), writing r6/r4*4 bytes --
# so this function RECEIVES the produced PCM.
PA="$PA,pc=0x0816bf48:pcmout,pc=0x0816c05c:pcmdump,pc=0x0816bed0:pcmhelper"
PA="$PA,pc=0x08016edc:hwpath,pc=0x08016ee8:hwgo,pc=0x08016f08:copy16,pc=0x08016f10:hwret"
# The two sibling threads of MeCCADecode that nobody has ever studied.
PA="$PA,pc=0x22004630:framedone,pc=0x22004754:endofstream,pc=0x08123cfc:decswitch"
# Which case of the decoder switch actually arrives? case = [msg+4]-0x52800000
PA="$PA,pc=0x08123d50:c0,pc=0x08123d64:c1_DECODE,pc=0x08123d48:c234,pc=0x08123d78:c5"
PA="$PA,pc=0x08123d8c:c6,pc=0x08123d98:c7,pc=0x08123dac:c89,pc=0x08123dc0:cdefault"

setsid "$BIN" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=$IMG,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234 \
  -plugin "$PLUG,$PA" >"$OUT/qmus_$TAG.out" 2>"$OUT/qmus_$TAG.err" &
echo "launched $TAG pid $!"
sleep 52
$IR wake e >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name L1 >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:2 L2 >/dev/null 2>&1
for i in 1 2 3 4 5 6 7 8; do
  $IR seq e:120 wait:1 ret:250 wait:2 z >/dev/null 2>&1
  if grep -aqE 'cpu[0-9]  (engine|dpump) ' "$LOG" 2>/dev/null; then echo "PROGRESS try $i"; break; fi
done
echo "=== 45s free-run (let the render loop breathe) ==="; sleep 45
echo "=== [$TAG] timeline ==="
grep -aE 'cpu[0-9]  (StartPlayback|dpump|engine|rendret|pumploop|pumpretry|pumppred|credithdlr|creditsig|mixer|copyent|copy32) ' "$LOG" | head -30
exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 2 && exec 3<&-
sleep 2
echo "=== [$TAG] PCTRACE summary ==="
sed -n '/PCTRACE summary/,$p' "$LOG" | head -22
echo "exceptions=$(grep -ac CPU_exception_handler "$OUT/qmus_$TAG.err")"
echo "resets=$(grep -c 'software reset' "$OUT/qmus_$TAG.err")"
pkill -9 -f "qmus_$TAG" 2>/dev/null
echo "DONE $TAG"
