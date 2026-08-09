#!/usr/bin/env python3
"""
emcore_s5l8702.py -- self-contained client for the emCORE USB debugger on an
iPod Classic 6G (Samsung S5L8702), speaking the *pre-r891* monitor protocol.

Why this file exists
--------------------
The official client (freemyipod SVN, mirrored at github.com/freemyipod/freemyipod)
does not work against a stock device out of the box:

  * The HEAD client only binds interfaces with bInterfaceSubClass/Protocol == 0/0
    and drives the monitor over EP0 control transfers.  That protocol landed in
    SVN r891.  The last *released* emCORE is r859, whose interface is ff/ff/ff
    with four bulk endpoints.  HEAD therefore refuses to enumerate the device.
  * The r859 client calls pyusb as ``dev.write(ep, data, interface, timeout)``.
    The ``interface`` parameter was removed in pyusb 1.0.0b1, so on any modern
    pyusb it raises TypeError.
  * ``misc.py`` uses ``inspect.getargspec``, removed in Python 3.11.

This implementation talks the r859 wire protocol directly and has no dependency
on the emCORE source tree.  Verified against a real device running
emCORE Debugger v0.2.3 r859 (see wiki/10-classic6g-emcore.md).

SAFETY
------
Only read-oriented commands are implemented.  Memory *writes* exist because the
hardware AES engine works in place on a RAM buffer, but they are constrained to
the device's malloc pool (DRAM).  The commands that mutate persistent state or
hand over control -- 23 bootflash write, 21 execimage, 24 runfirmware, and every
storage/NAND command -- are deliberately NOT implemented.  Command 22
``bootflashread`` IS implemented and is a pure read: it issues SPI opcode 3
(READ DATA), which cannot modify the flash.

Requires: pyusb (Debian/Ubuntu: python3-usb).  Needs write access to the USB
device node -- either run as root or install a udev rule:
    SUBSYSTEM=="usb", ATTR{idVendor}=="ffff", ATTR{idProduct}=="e000", MODE="0666"
"""

import argparse
import os
import struct
import sys

try:
    import usb.core
    import usb.util
except ImportError:  # keep the pure-parsing helpers importable without pyusb
    usb = None

VID = 0xFFFF
PID = 0xE000

HEADER = 16  # every monitor packet starts with a 16-byte little-endian header

# --- monitor command ids (subset; read-oriented only) -----------------------
CMD_GETINFO       = 1
CMD_READMEM       = 4
CMD_WRITEMEM      = 5
CMD_READDMA       = 6
CMD_WRITEDMA      = 7
CMD_BOOTFLASHREAD = 22
CMD_HWKEYAES      = 25
CMD_MALLOC        = 52
CMD_FREE          = 56

RESPONSE = {0: "INVALID", 1: "OK", 2: "UNSUPPORTED", 3: "BUSY"}

# hwkeyaes key indices, from Rockbox firmware/target/arm/s5l8702/crypto-s5l8702.h
KEY_GID = 1  # HWKEYAES_GKEY -- per-model key fused into the SoC; encrypts osos
KEY_UID = 2  # HWKEYAES_UKEY -- per-device unique key

AES_BLOCK = 16


class EmcoreError(Exception):
    pass


class Emcore:
    """Minimal emCORE monitor client (pre-r891, four bulk endpoints)."""

    def __init__(self, timeout=10000):
        if usb is None:
            raise EmcoreError("pyusb is required:  sudo apt-get install -y python3-usb")
        self.timeout = timeout
        dev = usb.core.find(idVendor=VID, idProduct=PID)
        if dev is None:
            raise EmcoreError("no emCORE device (ffff:e000) found")
        self.dev = dev
        dev.set_configuration()

        # r859 hands the endpoints out in a fixed order: command out, command in,
        # data out, data in.  emcore/trunk/usb/usb.c:696-699.
        eps = [ep.bEndpointAddress for cfg in dev for intf in cfg for ep in intf]
        if len(eps) < 4:
            raise EmcoreError(
                "expected 4 bulk endpoints, found %d -- this device is probably "
                "running emCORE >= r891, which uses the EP0 control protocol "
                "instead and is not supported here" % len(eps))
        self.ep_cout, self.ep_cin, self.ep_dout, self.ep_din = eps[:4]

        self.version = self._getinfo_version()
        self.packetsize = self._getinfo_packetsize()
        self.pool = self._getinfo_pool()

    # -- transport ----------------------------------------------------------
    def _cmd(self, packet, extra=0, timeout=None):
        """Send a command packet, read back the 16-byte header + `extra` bytes."""
        t = timeout or self.timeout
        n = self.dev.write(self.ep_cout, packet, t)
        if n != len(packet):
            raise EmcoreError("short write (%d of %d)" % (n, len(packet)))
        raw = bytes(self.dev.read(self.ep_cin, HEADER + extra, t))
        if len(raw) != HEADER + extra:
            raise EmcoreError("short read (%d of %d)" % (len(raw), HEADER + extra))
        status = struct.unpack("<I", raw[:4])[0]
        if status != 1:
            raise EmcoreError("device returned %s (%d) for command %d"
                              % (RESPONSE.get(status, "?"), status,
                                 struct.unpack("<I", packet[:4])[0]))
        return raw

    # -- identification -----------------------------------------------------
    # Response layouts below mirror libemcore.py (r859) exactly:
    #   cmd 1/0  ->  <I I B B B B I  (status, revision, major, minor, patch,
    #                                 swtypeid, hwtypeid)
    #   cmd 1/1  ->  <I H H I I      (status, coutmax, cinmax, doutmax, dinmax)
    #   cmd 1/2  ->  <I I I I        (status, lower, upper, unused)
    @staticmethod
    def decode_version(raw):
        _, rev, major, minor, patch, swtype, hwtype = struct.unpack("<IIBBBBI", raw)
        return {"revision": rev, "major": major, "minor": minor, "patch": patch,
                "swtypeid": swtype, "hwtypeid": hwtype}

    @staticmethod
    def decode_packetsize(raw):
        _, cout, cin, dout, din = struct.unpack("<IHHII", raw)
        return {"cout": cout, "cin": cin, "dout": dout, "din": din}

    @staticmethod
    def decode_pool(raw):
        _, lower, upper, _unused = struct.unpack("<IIII", raw)
        return (lower, upper)

    def _getinfo_version(self):
        return self.decode_version(self._cmd(struct.pack("<IIII", CMD_GETINFO, 0, 0, 0)))

    def _getinfo_packetsize(self):
        return self.decode_packetsize(self._cmd(struct.pack("<IIII", CMD_GETINFO, 1, 0, 0)))

    def _getinfo_pool(self):
        return self.decode_pool(self._cmd(struct.pack("<IIII", CMD_GETINFO, 2, 0, 0)))

    def describe(self):
        v = self.version
        hw = struct.pack("<I", v["hwtypeid"]).decode("latin1")
        return ("emCORE v%d.%d.%d r%d  swtype=%d  hw=0x%08X (%r)\n"
                "packet sizes: cout=%d cin=%d dout=%d din=%d\n"
                "malloc pool:  0x%08X - 0x%08X (%.2f MiB)"
                % (v["major"], v["minor"], v["patch"], v["revision"],
                   v["swtypeid"], v["hwtypeid"], hw,
                   self.packetsize["cout"], self.packetsize["cin"],
                   self.packetsize["dout"], self.packetsize["din"],
                   self.pool[0], self.pool[1],
                   (self.pool[1] - self.pool[0]) / 1048576.0))

    # -- memory -------------------------------------------------------------
    @staticmethod
    def _alignsplit(addr, size, blksize, align):
        """Split a transfer into (head, body, tail) so the body is DMA-aligned.

        Lifted from libemcore.Emcore._alignsplit (r859) -- the device's DMA path
        requires 16-byte alignment, the command-pipe path does not.
        """
        if size <= blksize:
            return (size, 0, 0)
        end = addr + size
        if addr & (align - 1):
            bodyaddr = (addr + min(size, blksize)) & ~(align - 1)
        else:
            bodyaddr = addr
        headsize = bodyaddr - addr
        if (size - headsize) & (align - 1):
            tailaddr = (end - min(end - bodyaddr, blksize) + align - 1) & ~(align - 1)
        else:
            tailaddr = end
        return (headsize, tailaddr - bodyaddr, end - tailaddr)

    def _readmem(self, addr, size):
        r = self._cmd(struct.pack("<IIII", CMD_READMEM, addr, size, 0), extra=size)
        return r[HEADER:]

    def _readdma(self, addr, size):
        self._cmd(struct.pack("<IIII", CMD_READDMA, addr, size, 0))
        return bytes(self.dev.read(self.ep_din, size, self.timeout))

    def _writemem(self, addr, data):
        self._cmd(struct.pack("<IIII", CMD_WRITEMEM, addr, len(data), 0) + data)

    def _writedma(self, addr, data):
        self._cmd(struct.pack("<IIII", CMD_WRITEDMA, addr, len(data), 0))
        n = self.dev.write(self.ep_dout, data, self.timeout)
        if n != len(data):
            raise EmcoreError("short DMA write")

    def read(self, addr, size):
        cin_max = self.packetsize["cin"] - HEADER
        din_max = self.packetsize["din"]
        head, body, tail = self._alignsplit(addr, size, cin_max, AES_BLOCK)
        out = b""
        if head:
            out += self._readmem(addr, head)
            addr += head
        while body > 0:
            if body >= 2 * cin_max:
                n = min(body, din_max)
                out += self._readdma(addr, n)
            else:
                n = min(body, cin_max)
                out += self._readmem(addr, n)
            addr += n
            body -= n
        if tail:
            out += self._readmem(addr, tail)
        return out

    def write(self, addr, data):
        """Write to device RAM.  Refuses anything outside the malloc pool."""
        lo, hi = self.pool
        if not (lo <= addr and addr + len(data) <= hi):
            raise EmcoreError(
                "refusing to write outside the malloc pool 0x%08X-0x%08X "
                "(asked for 0x%08X+0x%X)" % (lo, hi, addr, len(data)))
        cout_max = self.packetsize["cout"] - HEADER
        dout_max = self.packetsize["dout"]
        head, body, tail = self._alignsplit(addr, len(data), cout_max, AES_BLOCK)
        off = 0
        if head:
            self._writemem(addr, data[off:off + head])
            off += head
            addr += head
        while body > 0:
            if body >= 2 * cout_max:
                n = min(body, dout_max)
                self._writedma(addr, data[off:off + n])
            else:
                n = min(body, cout_max)
                self._writemem(addr, data[off:off + n])
            off += n
            addr += n
            body -= n
        if tail:
            self._writemem(addr, data[off:off + tail])

    # -- heap ---------------------------------------------------------------
    def malloc(self, size):
        r = self._cmd(struct.pack("<IIII", CMD_MALLOC, size, 0, 0))
        ptr = struct.unpack("<IIII", r)[1]
        if not ptr:
            raise EmcoreError("malloc(%d) failed" % size)
        return ptr

    def free(self, ptr):
        self._cmd(struct.pack("<IIII", CMD_FREE, ptr, 0, 0))

    # -- hardware AES -------------------------------------------------------
    def aes(self, addr, size, keyidx, encrypt=False, timeout=600000):
        """Run the S5L8702 AES engine in place over [addr, addr+size).

        AES-128-CBC.  emCORE zeroes AESIV at the top of every hwkeyaes() call
        (emcore/target/ipodnano3g/hwkeyaes.c), so each invocation restarts CBC
        with IV = 0.  `size` must be a multiple of 16.
        """
        if size % AES_BLOCK:
            raise EmcoreError("AES size must be a multiple of %d" % AES_BLOCK)
        pkt = struct.pack("<IBBHII", CMD_HWKEYAES,
                          1 if encrypt else 0, 0, keyidx, addr, size)
        self._cmd(pkt, timeout=timeout)

    # -- SPI NOR boot flash (read only) -------------------------------------
    def bootflashread(self, memaddr, flashaddr, size, timeout=60000):
        """Copy `size` bytes of SPI NOR into device RAM.  Issues SPI opcode 3
        (READ DATA) -- it cannot modify the flash."""
        self._cmd(struct.pack("<IIII", CMD_BOOTFLASHREAD, memaddr, flashaddr, size),
                  timeout=timeout)


# --------------------------------------------------------------------------
def decrypt_payload(dev, ciphertext, keyidx=KEY_GID, chunk=0x100000, progress=None):
    """Decrypt an Apple firmware payload with the SoC's fused key.

    The engine restarts CBC with IV = 0 on every call, so a chunked run returns
    a wrong *first* block per chunk and correct blocks thereafter.  CBC is
    P[i] = D(C[i]) ^ C[i-1], hence the repair for the seam is simply

        P[0]_true = P[0]_returned ^ (last ciphertext block of previous chunk)

    Bytes past the last whole AES block are copied through untouched.
    """
    enc_len = len(ciphertext) & ~(AES_BLOCK - 1)
    buf = dev.malloc(min(chunk, max(enc_len, AES_BLOCK)))
    out = bytearray()
    try:
        off = 0
        while off < enc_len:
            n = min(chunk, enc_len - off)
            dev.write(buf, ciphertext[off:off + n])
            dev.aes(buf, n, keyidx)
            pt = bytearray(dev.read(buf, n))
            if off:
                prev = ciphertext[off - AES_BLOCK:off]
                for i in range(AES_BLOCK):
                    pt[i] ^= prev[i]
            out += pt
            off += n
            if progress:
                progress(off, enc_len)
    finally:
        dev.free(buf)
    out += ciphertext[enc_len:]
    return bytes(out)


def _hexdump(data, base=0):
    lines = []
    for i in range(0, len(data), 16):
        row = data[i:i + 16]
        h = " ".join("%02x" % b for b in row).ljust(47)
        a = "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in row)
        lines.append("%08X: %s |%s|" % (base + i, h, a))
    return "\n".join(lines)


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("info", help="identify the device")

    q = sub.add_parser("hexdump", help="hexdump a region of device memory")
    q.add_argument("addr", type=lambda s: int(s, 0))
    q.add_argument("size", type=lambda s: int(s, 0))

    q = sub.add_parser("dump", help="dump device memory to a file")
    q.add_argument("addr", type=lambda s: int(s, 0))
    q.add_argument("size", type=lambda s: int(s, 0))
    q.add_argument("out")

    q = sub.add_parser("norread", help="dump the SPI NOR boot flash to a file")
    q.add_argument("out")
    q.add_argument("--size", type=lambda s: int(s, 0), default=0x100000)

    q = sub.add_parser("decrypt", help="decrypt a firmware payload with the GID key")
    q.add_argument("infile")
    q.add_argument("outfile")
    q.add_argument("--key", type=int, default=KEY_GID,
                   choices=(KEY_GID, KEY_UID), help="1 = GID (default), 2 = UID")

    a = p.parse_args()
    dev = Emcore()

    if a.cmd == "info":
        print(dev.describe())

    elif a.cmd == "hexdump":
        print(_hexdump(dev.read(a.addr, a.size), a.addr))

    elif a.cmd == "dump":
        data = dev.read(a.addr, a.size)
        open(a.out, "wb").write(data)
        print("wrote %d bytes to %s" % (len(data), a.out))

    elif a.cmd == "norread":
        buf = dev.malloc(a.size)
        try:
            step = 0x40000
            for off in range(0, a.size, step):
                dev.bootflashread(buf + off, off, min(step, a.size - off))
            data = dev.read(buf, a.size)
        finally:
            dev.free(buf)
        open(a.out, "wb").write(data)
        print("wrote %d bytes of NOR to %s" % (len(data), a.out))

    elif a.cmd == "decrypt":
        ct = open(a.infile, "rb").read()

        def prog(done, total):
            sys.stdout.write("\r  %d / %d  (%.1f%%)" % (done, total, 100.0 * done / total))
            sys.stdout.flush()

        pt = decrypt_payload(dev, ct, keyidx=a.key, progress=prog)
        sys.stdout.write("\n")
        open(a.outfile, "wb").write(pt)
        print("wrote %d bytes to %s" % (len(pt), a.outfile))


if __name__ == "__main__":
    main()
