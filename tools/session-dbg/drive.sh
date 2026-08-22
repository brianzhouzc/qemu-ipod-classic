#!/bin/bash
set +e
cd /mnt/c/Desarrollo/qemu-ios; IR="python3 tools/ipod_classic/iremote.py"
$IR wake e 4000 >/dev/null 2>&1
$IR press ret 250 -u -w e --region right -r 12 -t 3000 -s 4 --name WGa >/dev/null 2>&1
$IR seq e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:1 e:120 wait:2 WGb >/dev/null 2>&1
for t in 1 2 3 4 5 6; do
  $IR seq e:120 ret:250 wait:2 >/dev/null 2>&1
  if grep -q StartPlayback /home/dmontero/6g/wg.log 2>/dev/null; then echo "StartPlayback FIRED after try $t"; break; fi
done
