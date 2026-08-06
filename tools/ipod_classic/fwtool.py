#!/usr/bin/env python3
"""Parse and extract iPod Classic 6G (S5L8702) firmware images.

Handles two nested container formats:

  1. The classic iPod firmware partition ("]ih[" header + image directory).
     This is what the `Firmware-XX` payload inside an .ipsw is.
  2. The per-image S5L870x "8702" header that wraps each osos/aupd/rsrc blob.

Usage:
    python fwtool.py list    <Firmware-file>
    python fwtool.py extract <Firmware-file> <outdir>
"""

import os
import struct
import sys

FW_MAGIC_OFF = 0x100
FW_MAGIC = b"]ih["

# Directory entry, 40 bytes, little endian. Field names follow ipodpatcher.
DIRENT_FMT = "<4s4s8I"
DIRENT_SIZE = struct.calcsize(DIRENT_FMT)
assert DIRENT_SIZE == 40

# S5L870x image header. The header struct itself is small but the whole area is
# padded out to 0x800 bytes; the payload starts right after. All size/offset
# fields inside the header are relative to the start of the payload, not to the
# start of the file.
IMG_HDR_SIZE = 0x800


def _fourcc(raw):
    """Decode a reversed four-character code, showing junk as dots."""
    return "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in raw[::-1])


def _is_fourcc(raw):
    """True if all four bytes are printable, i.e. this looks like a real tag."""
    return all(0x20 <= b < 0x7F for b in raw)


class DirEntry:
    def __init__(self, raw, index):
        (dev, typ, self.id, self.dev_offset, self.length, self.addr,
         self.entry_offset, self.checksum, self.version,
         self.load_addr) = struct.unpack(DIRENT_FMT, raw)
        self.index = index
        self.dev = _fourcc(dev)
        self.type = _fourcc(typ)

    def __str__(self):
        return ("[%d] %-4s dev=%-4s off=0x%08x len=0x%08x (%d) addr=0x%08x "
                "entry=0x%08x chk=0x%08x vers=0x%08x" %
                (self.index, self.type, self.dev, self.dev_offset, self.length,
                 self.length, self.addr, self.entry_offset, self.checksum,
                 self.version))


class ImageHeader:
    """The "8702" header in front of each image payload.

    Layout, verified against the osos and aupd images of iPod_24.1.1.2.ipsw:

        0x00  char[4]  "8702"
        0x04  char[3]  "1.0"
        0x07  uint8    format / encryption type (0x03 on retail images)
        0x08  uint32   unknown, zero
        0x0c  uint32   payload size, unpadded
        0x10  uint32   footer_offset + footer_size
        0x14  uint32   footer offset, relative to the payload start
        0x18  uint32   footer size
        0x40  byte[20] SHA-1
        ...            zero padding up to 0x800
    """

    def __init__(self, raw):
        self.magic = raw[0:4].decode("latin1")  # "8702", stored unreversed
        self.version = raw[4:7].decode("latin1")  # "1.0"
        self.format = raw[7]                    # 0x03 seen on retail images
        (self.unknown_08, self.data_size, self.body_size, self.footer_offset,
         self.footer_size) = struct.unpack("<5I", raw[8:0x1c])
        self.sha1 = raw[0x40:0x54]

    @property
    def encrypted(self):
        # 0x03 is what Apple ships; a plain/unencrypted variant would differ.
        return self.format == 0x03

    @property
    def total_size(self):
        """Size of the whole image on disk, header included."""
        return IMG_HDR_SIZE + self.body_size

    def __str__(self):
        return ("magic=%s ver=%s fmt=0x%02x payload=0x%x footer=0x%x+0x%x "
                "total=0x%x sha1=%s" %
                (self.magic, self.version, self.format, self.data_size,
                 self.footer_offset, self.footer_size, self.total_size,
                 self.sha1.hex()))

    def check(self):
        """Return a list of consistency problems, empty if the header adds up."""
        problems = []
        if self.data_size > self.footer_offset:
            problems.append("payload 0x%x overruns footer at 0x%x" %
                            (self.data_size, self.footer_offset))
        if self.footer_offset + self.footer_size != self.body_size:
            problems.append("footer_offset+footer_size 0x%x != 0x%x at +0x10" %
                            (self.footer_offset + self.footer_size,
                             self.body_size))
        return problems


def _looks_like_dirent(data, off):
    """Cheap validity test for a directory entry at a given offset."""
    raw = data[off:off + DIRENT_SIZE]
    if len(raw) < DIRENT_SIZE:
        return False
    if not (_is_fourcc(raw[0:4]) and _is_fourcc(raw[4:8])):
        return False
    dev_offset, length = struct.unpack("<2I", raw[0x0c:0x14])
    return 0 < dev_offset < len(data) and 0 < length <= len(data)


def read_directory(data):
    if data[FW_MAGIC_OFF:FW_MAGIC_OFF + 4] != FW_MAGIC:
        raise ValueError("no ]ih[ magic at 0x%x" % FW_MAGIC_OFF)
    dir_offset, = struct.unpack("<I", data[FW_MAGIC_OFF + 4:FW_MAGIC_OFF + 8])

    # The offset in the header is not relative to the start of this buffer, and
    # what it *is* relative to varies: a Firmware-XX payload pulled out of an
    # .ipsw puts the directory 0x1000 further along, while the firmware
    # partition read off an iPod Video has it 0x200 further along. Rather than
    # keep a table of fudge factors, try the known ones and then fall back to
    # scanning for the first thing that parses as a directory entry.
    candidates = [dir_offset + delta for delta in (0x1000, 0x200, 0x0)]
    for base in candidates:
        if _looks_like_dirent(data, base):
            break
    else:
        base = None
        for off in range(0x1000, min(len(data), 0x20000), 4):
            if _looks_like_dirent(data, off):
                base = off
                break
        if base is None:
            raise ValueError("could not locate image directory")

    entries = []
    off = base
    while off + DIRENT_SIZE <= len(data):
        raw = data[off:off + DIRENT_SIZE]
        if raw[:4] == b"\0\0\0\0" or raw[:4] == b"\xff\xff\xff\xff":
            break
        # The directory is not length-prefixed and is followed by whatever
        # happened to be on the disk, so stop as soon as an entry stops looking
        # like one. Both four-character codes must be printable and the image
        # has to start inside the file.
        if not (_is_fourcc(raw[0:4]) and _is_fourcc(raw[4:8])):
            break
        entry = DirEntry(raw, len(entries))
        if entry.dev_offset >= len(data):
            break
        entries.append(entry)
        off += DIRENT_SIZE
    return base, entries


def entropy(buf):
    if not buf:
        return 0.0
    import math
    counts = [0] * 256
    for b in buf:
        counts[b] += 1
    n = len(buf)
    return -sum((c / n) * math.log2(c / n) for c in counts if c)


def cmd_list(path):
    data = open(path, "rb").read()
    dir_base, entries = read_directory(data)
    print("file       : %s (0x%x bytes)" % (path, len(data)))
    print("directory  : 0x%x, %d entries" % (dir_base, len(entries)))
    for e in entries:
        print(e)
        if data[e.dev_offset:e.dev_offset + 4] == b"8702":
            hdr = ImageHeader(data[e.dev_offset:e.dev_offset + IMG_HDR_SIZE])
            print("      %s" % hdr)
            for p in hdr.check():
                print("      WARNING: %s" % p)
            # The directory records the image size minus the 0x1000 that the
            # header padding and alignment add on top of the payload.
            if e.length != hdr.total_size - 0x1000:
                print("      WARNING: dir len 0x%x != total 0x%x - 0x1000" %
                      (e.length, hdr.total_size))
            payload = data[e.dev_offset + IMG_HDR_SIZE:
                           e.dev_offset + IMG_HDR_SIZE + 0x4000]
            ent = entropy(payload)
            print("      payload entropy (first 16K): %.3f bits/byte%s" %
                  (ent, "  -> encrypted" if ent > 7.9 else ""))
        else:
            head = data[e.dev_offset:e.dev_offset + 8]
            print("      (no 8702 header; first bytes %s)" % head.hex())


def cmd_extract(path, outdir):
    data = open(path, "rb").read()
    _, entries = read_directory(data)
    os.makedirs(outdir, exist_ok=True)
    for e in entries:
        size = e.length
        if data[e.dev_offset:e.dev_offset + 4] == b"8702":
            size = ImageHeader(data[e.dev_offset:e.dev_offset +
                                    IMG_HDR_SIZE]).total_size
        blob = data[e.dev_offset:e.dev_offset + size]
        raw_path = os.path.join(outdir, "%d_%s.img" % (e.index, e.type))
        with open(raw_path, "wb") as f:
            f.write(blob)
        print("wrote %s (0x%x bytes)" % (raw_path, len(blob)))

        if blob[0:4] == b"8702":
            hdr = ImageHeader(blob[:IMG_HDR_SIZE])
            body = blob[IMG_HDR_SIZE:IMG_HDR_SIZE + hdr.data_size]
            body_path = os.path.join(outdir, "%d_%s.payload" % (e.index, e.type))
            with open(body_path, "wb") as f:
                f.write(body)
            print("  payload -> %s (0x%x bytes)" % (body_path, len(body)))

            foot = blob[hdr.footer_offset:hdr.footer_offset + hdr.footer_size]
            if foot:
                foot_path = os.path.join(outdir,
                                         "%d_%s.footer" % (e.index, e.type))
                with open(foot_path, "wb") as f:
                    f.write(foot)
                print("  footer  -> %s (0x%x bytes)" % (foot_path, len(foot)))


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    cmd = argv[1]
    if cmd == "list":
        cmd_list(argv[2])
    elif cmd == "extract":
        if len(argv) < 4:
            print(__doc__)
            return 1
        cmd_extract(argv[2], argv[3])
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
