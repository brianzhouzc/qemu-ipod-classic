#!/bin/sh
# Audit helper: boot the retailOS, screendump, and report what stderr said.
#   sh audit_run.sh <tag> <seconds> [extra qemu args...]
# Writes /tmp/aud/<tag>.ppm, .err, .mon
set -e
cd /mnt/c/Desarrollo/qemu-ios
TAG=$1; shift
SECS=$1; shift
mkdir -p /tmp/aud
( sleep "$SECS"; printf 'screendump /tmp/aud/%s.ppm\n' "$TAG"; sleep 3; \
  printf 'info registers\n'; sleep 2; printf 'quit\n' ) | \
  ./build/qemu-system-arm -M iPod-Classic \
      -kernel assets/classic6g/osos_decrypted.bin \
      -display none -monitor stdio "$@" \
      > "/tmp/aud/$TAG.mon" 2> "/tmp/aud/$TAG.err" || true
echo "=== $TAG: pixels ==="
python3 tools/ipod_classic/pixcount.py "/tmp/aud/$TAG.ppm" | head -3
echo "=== $TAG: stderr (deduped) ==="
sort "/tmp/aud/$TAG.err" | uniq -c | sort -rn | head -20
echo "=== $TAG: registers ==="
grep -E '^R1[25]|^PSR' "/tmp/aud/$TAG.mon" | head -4
