#!/bin/bash
# Validation of pctrace register-read backport: boot only (no nav), read GPRs
# at the strcmp entry 0x08328090 (a call target => TB head, hit constantly).
# Sane result => lr in code range, r0/r1 look like pointers, values differ.
set +e
ROOT=/mnt/c/Desarrollo/qemu-ios; OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
PLUG=$ROOT/build/contrib/plugins/libpctrace.so
LOG=/home/dmontero/6g/regval.log
pkill -9 -x qmusv 2>/dev/null; sleep 1
cp "$ROOT/build/qemu-system-arm" "$ROOT/build/qmusv"
PA="nodefault=on,regcap=8,regpc=0x08328090:strcmp_entry,out=$LOG"
setsid "$ROOT/build/qmusv" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=/home/dmontero/6g/Emulti.img,file.locking=off" \
  -snapshot -display none -serial null \
  -monitor "tcp:127.0.0.1:4467,server,nowait" \
  -plugin "$PLUG,$PA" >/home/dmontero/6g/regval.qout 2>/home/dmontero/6g/regval.qerr &
sleep 22
pkill -9 -x qmusv 2>/dev/null; sleep 1
echo "=== qerr (dlopen errors if any) ==="; grep -i "error\|symbol\|undefined\|plugin" /home/dmontero/6g/regval.qerr | head
echo "=== log head ==="; head -3 $LOG
echo "=== REGPC lines ==="; grep REGPC $LOG | head -12
echo "=== summary ==="; sed -n "/register-read watches/,\$p" $LOG
