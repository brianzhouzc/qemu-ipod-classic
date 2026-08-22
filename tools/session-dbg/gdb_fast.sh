#!/bin/bash
set +e
cd /mnt/c/Desarrollo/qemu-ios; IR='python3 tools/ipod_classic/iremote.py'
~/dbg/launch_wg.sh >/dev/null 2>&1
echo boot28; sleep 28
$IR wake e 3000 >/dev/null 2>&1
pgrep -x qmus >/dev/null || { echo DIED-boot; exit 1; }
# arm gdb (break StartPlayback+engine, follow, enum)
setsid gdb-multiarch -batch -x ~/dbg/catch_final.py > /home/dmontero/6g/gdbf.out 2>&1 &
G=$!
sleep 2
# fast navigate + select
$IR seq w:120 e:120 e:120 e:120 e:120 e:120 e:120 GF >/dev/null 2>&1
( $IR press ret 200 -u -w e -r 8 -t 1500 -s 1 --name GFSEL >/dev/null 2>&1 ) &
D=$!
for w in $(seq 1 40); do kill -0 $G 2>/dev/null || { echo "gdb done w=$w"; break; }; sleep 1; done
kill -9 $G $D 2>/dev/null
echo '===== GDB OUT ====='
cat /home/dmontero/6g/gdbf.out | grep -avE 'auto-load|add-auto|safe path|set auto|line to|For more|info "|Warning|deprecated|No executable|Try using|declined|To enable|To completely|section in|E.g.|Auto-load'
echo '===== qmus ====='; pgrep -x qmus || echo dead
