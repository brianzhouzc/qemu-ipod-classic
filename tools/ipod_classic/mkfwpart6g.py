#!/usr/bin/env python3
"""Re-lay an iPod Classic 6G firmware partition for a different block size.

The Firmware-XX payload inside an .ipsw is a firmware-partition image laid out
for a **4096-byte** block, which is what an iPod Classic uses (see
wiki/11).  A QEMU IDE disk is 512-byte, so the retailOS's own directory reader
(0x0805712c) would look for the directory at partition+0x4200 and find zeros.

This rewrites the container for a chosen block size, keeping the images
byte-identical.  The layout rules are taken straight out of that function:

    directory block = partition_start + diroffset/bs + 1
    image block     = partition_start + devOffset/bs + 1
    diroffset, devOffset and len must all be multiples of bs

    mkfwpart6g.py <in-firmware-payload> <out> [--in-bs 4096] [--out-bs 512]
"""
import argparse
import struct

DIRENT = "<4s4sIIIIIIII"          # dev, type, id, devOffset, len, addr,
assert struct.calcsize(DIRENT) == 40   # entryOffset, checksum, vers, loadAddr


def parse(buf, bs):
    if buf[0x100:0x104] != b"]ih[":
        raise SystemExit("no ]ih[ at 0x100")
    diroff = struct.unpack_from("<I", buf, 0x104)[0]
    if diroff % bs:
        raise SystemExit("diroffset 0x%x is not a multiple of %d" % (diroff, bs))
    dirpos = (diroff // bs + 1) * bs
    ents = []
    for i in range(12):
        raw = buf[dirpos + i * 40:dirpos + (i + 1) * 40]
        if raw[0:4] not in (b"!ATA", b"DNAN"):
            break
        f = list(struct.unpack(DIRENT, raw))
        start = (f[3] // bs + 1) * bs
        ents.append((f, buf[start:start + f[4]]))
    return diroff, ents


def emit(ents, bs, diroff=0x4000):
    """Build a partition image whose every offset/length is a multiple of bs."""
    dirpos = (diroff // bs + 1) * bs
    out = bytearray(dirpos + bs)          # header block .. directory block
    # Sector 0: the Apple copyright block ipodpatcher checks for, then ]ih[.
    stop = ("{{~~  /-----\\   {{~~ /       \\  {{~~|         | "
            "{{~~| S T O P | {{~~|         | {{~~ \\       /  "
            "{{~~  \\-----/   Copyright(C) 2001 Apple Computer, Inc."
            + "-" * 95).encode()
    out[0:len(stop)] = stop
    out[0x100:0x104] = b"]ih["
    struct.pack_into("<I", out, 0x104, diroff)
    struct.pack_into("<HH", out, 0x108, 0x010c, 3)   # copied from the .ipsw

    entries = []
    for f, data in ents:
        padded = data + b"\0" * (-len(data) % bs)
        # image payload goes one block after devOffset; devOffset is the byte
        # position of the block *before* it, counted from the partition start.
        devoff = len(out)
        out += b"\xff" * bs                # the block the "+1" skips over
        out += padded
        g = list(f)
        g[3] = devoff
        g[4] = len(padded)
        entries.append(g)

    for i, g in enumerate(entries):
        struct.pack_into(DIRENT, out, dirpos + i * 40, *g)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--in-bs", type=int, default=4096)
    ap.add_argument("--out-bs", type=int, default=512)
    a = ap.parse_args()

    buf = open(a.src, "rb").read()
    diroff, ents = parse(buf, a.in_bs)
    print("in : diroffset 0x%x, %d images (block %d)" % (diroff, len(ents), a.in_bs))
    for f, data in ents:
        print("     %-4s id=%d devOffset=0x%08x len=0x%08x addr=0x%08x entry=0x%x"
              % (f[1][::-1].decode("latin1"), f[2], f[3], f[4], f[5], f[6]))
    img = emit(ents, a.out_bs, diroff)
    open(a.dst, "wb").write(img)

    # Validate by replaying the guest's own reader on the result.
    d2, e2 = parse(img, a.out_bs)
    print("out: diroffset 0x%x, %d images (block %d), %d bytes"
          % (d2, len(e2), a.out_bs, len(img)))
    for f, data in e2:
        assert f[4] % a.out_bs == 0 and f[3] % a.out_bs == 0
        print("     %-4s id=%d devOffset=0x%08x len=0x%08x  data[0:4]=%s"
              % (f[1][::-1].decode("latin1"), f[2], f[3], f[4], data[:4].hex()))


if __name__ == "__main__":
    main()
