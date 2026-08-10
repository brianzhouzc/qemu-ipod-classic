#!/bin/sh
# Audit helper: boot, wait, then read guest physical words through the monitor.
#   sh audit_peek.sh <tag> <seconds> '<addr1> <addr2> ...' [qemu args...]
set -e
cd /mnt/c/Desarrollo/qemu-ios
TAG=$1; shift
SECS=$1; shift
ADDRS=$1; shift
mkdir -p "$HOME/aud"
{
    sleep "$SECS"
    for a in $ADDRS; do printf 'xp /1wx %s\n' "$a"; done
    sleep 3
    printf 'quit\n'
} | ./build/qemu-system-arm -M iPod-Classic \
        -kernel assets/classic6g/osos_decrypted.bin \
        -display none -monitor stdio "$@" \
        > "$HOME/aud/$TAG.peek" 2> "$HOME/aud/$TAG.peekerr" || true
tr '\r' '\n' < "$HOME/aud/$TAG.peek" | grep -E '^0x[0-9a-f]+:' || \
    cat "$HOME/aud/$TAG.peek"
