#!/bin/bash
# Assault #14: does the guest ARM the SM1 engine interrupt, and which path does the
# render take?
#
# Decoded this session:
#   0x0808170c  render entry; reads [engobj+0x47] and if 0 branches to 0x08082020
#   0x08081734  ... the FULL path (gate passed)
#   0x08082020  ... the EARLY-EXIT path (gate == 0)
#   0x080820e4  bl 0x08017364          <- inside the early-exit path
#   0x08017364  ldr r1,=0x38500000 ; mov r0,#4 ; str r0,[r1,#0xa8c]
#               i.e. WRITE 4 (bit 2) TO SM1+0xa8c -- the engine's interrupt ENABLE
#   0x08017378  the VIC index-18 handler: reads SM1 status, tests bit 2 (tst r0,#4),
#               and on success signals the credit event 0x22008c58 (0x080173cc)
#
# Hypothesis: the firmware arms the SM1 "frame/buffer done" interrupt and then waits
# for it. Our SM1 is a latch with no interrupt line, so the interrupt never arrives,
# the render never returns, the pump times out and tears the codec down.
#
# If sm1en > 0 this is confirmed and gives us a SELF-GATING, faithful trigger: start
# asserting VIC line 18 exactly when the guest writes bit 2 to SM1+0xa8c.
set +e
TAG=${1:-rtrace}
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
pgrep -f "qmus_$TAG " >/dev/null && pkill -9 -f "qmus_$TAG " ; sleep 1
cp "$ROOT/build/qemu-system-arm" "$BIN"
for _ in $(seq 1 30); do ss -ltn 2>/dev/null | grep -q ":4455 " || break; sleep 2; done  # wait for the monitor port

export AUDIOPACE=1
for kv in "$@"; do export "$kv"; done

PA="nodefault=on,always=on,cap=8,window=300000,out=$LOG"
PA="$PA,trig=0x08016f14"
PA="$PA,pc=0x08150d84:StartPlayback,pc=0x22006118:dpump,pc=0x0808170c:engine"
PA="$PA,pc=0x08081734:rfull,pc=0x08082020:rearly,pc=0x080820e4:callsm1en"
PA="$PA,pc=0x08017364:sm1en,pc=0x08017378:credithdlr,pc=0x080173cc:creditsig"
PA="$PA,pc=0x22006304:rendret,pc=0x0808af58:pumppred,pc=0x08016eb4:copy32"
PA="$PA,pc=0x08081af8:clkspin,pc=0x0809a74c:mixer"
PA="$PA,trig=0x08016f14"
PA="$PA,pc=0x08081a9c:mx1,pc=0x08081acc:mx2,pc=0x08081b68:mx3,pc=0x08081bbc:mx4"
PA="$PA,pc=0x08081bdc:mx5,pc=0x08081c28:mx6,pc=0x08081c54:mx7,pc=0x08081cb0:mx8"
PA="$PA,pc=0x080818e4:tconv,pc=0x08085ea0:memcpy,pc=0x08085ec4:memcpyret"
PA="$PA,regpc=0x08085ea0:mcargs,regpc=0x08016e64:posret,regcap=10"

setsid "$BIN" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=$IMG,file.locking=off" -snapshot \
  -display none -serial null -monitor "tcp:127.0.0.1:4455,server,nowait" -gdb tcp::1234 \
  -plugin "$PLUG,$PA" >"$OUT/qmus_$TAG.out" 2>"$OUT/qmus_$TAG.err" &
echo "launched $TAG pid $!"
( timeout 190 gdb-multiarch -q -batch -x tools/session-dbg/memcpy_probe.gdb \n    >"$OUT/gdb_$TAG.out" 2>"$OUT/gdb_$TAG.err" ) &
sleep 3
sleep 52
$IR wake e >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 15 -t 3000 -s 5 --name L1 >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:2 L2 >/dev/null 2>&1
for i in 1 2 3 4 5 6 7 8; do
  $IR seq e:120 wait:1 ret:250 wait:2 z >/dev/null 2>&1
  if grep -aqE 'cpu[0-9]  (engine|dpump) ' "$LOG" 2>/dev/null; then echo "PROGRESS try $i"; break; fi
done
echo "=== 12s free-run ==="; sleep 12
echo "=== [$TAG] timeline ==="
grep -aE 'cpu[0-9]  (StartPlayback|dpump|engine|rfull|rearly|callsm1en|sm1en|credithdlr|creditsig|rendret|pumppred|clkspin|mixer|copy32) ' "$LOG" | head -30
exec 3<>/dev/tcp/127.0.0.1/4455 && echo quit >&3 && sleep 2 && exec 3<&-
sleep 2
echo "=== [$TAG] PCTRACE summary ==="
sed -n '/PCTRACE summary/,$p' "$LOG" | head -24
echo "exceptions=$(grep -ac CPU_exception_handler "$OUT/qmus_$TAG.err")"
echo "resets=$(grep -c 'software reset' "$OUT/qmus_$TAG.err")"
pgrep -f "qmus_$TAG " >/dev/null && pkill -9 -f "qmus_$TAG "
echo "DONE $TAG"
