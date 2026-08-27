#!/usr/bin/env bash
# Ask the firmware itself what FireWire GUID it has.
#
# On a real iPod the device writes iPod_Control/Device/SysInfo on its own; that
# file is how iTunes learns the GUID in the first place. Our machine models no NOR
# and there is no SysCfg block anywhere on the disk, so the GUID the firmware
# believes in cannot be assumed to be the one in the NOR dump
# (000A270013118B17) -- it has to be observed.
#
# So: boot a scratch copy WITHOUT -snapshot, let retailOS come up and write
# whatever it writes, shut down through the monitor so FAT is flushed, and read
# the file back. Whatever GUID it contains is the one the database must be signed
# with, because it is the one the firmware will hash.
set -u
ROOT=$HOME/qemu-ios
OSOS=$ROOT/assets/classic6g/osos_decrypted.bin
SRC=${SRC:-$HOME/6g/Emulti.img}
SCRATCH=$HOME/6g/ask_device.img
SECS=${SECS:-120}
MON=4466

pkill -9 -x qemu-system-arm 2>/dev/null
cp "$SRC" "$SCRATCH"

# Marker the firmware must overwrite for us to believe it wrote anything.
umount /mnt/ipod6g 2>/dev/null
mkdir -p /mnt/ipod6g
mount -o rw,loop,offset=92962816 "$SCRATCH" /mnt/ipod6g || exit 1
printf 'FirewireGuid: DEADBEEFDEADBEEF\nModelNumStr: MB147\n' \
    > /mnt/ipod6g/iPod_Control/Device/SysInfo
sync
umount /mnt/ipod6g

"$ROOT/build/qemu-system-arm" -M iPod-Classic -kernel "$OSOS" \
  -drive "if=ide,format=raw,file=$SCRATCH,file.locking=off" \
  -display none -serial null -monitor "tcp:127.0.0.1:$MON,server,nowait" \
  >"$HOME/6g/ask_device.out" 2>"$HOME/6g/ask_device.err" &
QPID=$!
echo "booted (pid $QPID), giving it ${SECS}s"
sleep "$SECS"

# Clean shutdown so the FAT driver flushes.
exec 3<>/dev/tcp/127.0.0.1/$MON && echo quit >&3 && sleep 5 && exec 3<&-
sleep 3
kill -9 $QPID 2>/dev/null
wait $QPID 2>/dev/null

mount -o ro,loop,offset=92962816 "$SCRATCH" /mnt/ipod6g || exit 1
echo "=== SysInfo after boot ==="
cat /mnt/ipod6g/iPod_Control/Device/SysInfo
echo "=== Device dir ==="
ls -la /mnt/ipod6g/iPod_Control/Device/
echo "=== iTunes dir ==="
ls -la /mnt/ipod6g/iPod_Control/iTunes/
umount /mnt/ipod6g
