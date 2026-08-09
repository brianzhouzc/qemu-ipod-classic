#!/usr/bin/env python3
"""
Offline tests for emcore_s5l8702.py -- no device required.

The expected values are the ones actually observed on the user's iPod Classic
6G (emCORE Debugger v0.2.3 r859) on 2026-08-09; see wiki/10-classic6g-emcore.md.
Keeping them pinned here means a refactor of the response decoders cannot
silently drift away from the real wire format.

Run:  python3 tools/ipod_classic/test_emcore_s5l8702.py
"""

import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emcore_s5l8702 import Emcore, decrypt_payload, AES_BLOCK, KEY_GID  # noqa: E402


class TestResponseDecoders(unittest.TestCase):
    """Pin the 16-byte monitor response layouts to observed hardware values."""

    def test_version(self):
        # status=OK, revision=859, 0.2.3, swtypeid=2, hwtypeid='IPCL'
        raw = struct.pack("<IIBBBBI", 1, 859, 0, 2, 3, 2, 0x4C435049)
        self.assertEqual(len(raw), 16)
        v = Emcore.decode_version(raw)
        self.assertEqual(v["revision"], 859)
        self.assertEqual((v["major"], v["minor"], v["patch"]), (0, 2, 3))
        self.assertEqual(v["swtypeid"], 2)          # 2 == "emCORE Debugger"
        self.assertEqual(v["hwtypeid"], 0x4C435049)
        # PLATFORM_ID 0x4c435049 is the little-endian packing of "IPCL"
        # (emcore/target/ipodclassic/target.h:28) -- iPod Classic
        self.assertEqual(struct.pack("<I", v["hwtypeid"]), b"IPCL")

    def test_packetsize(self):
        raw = struct.pack("<IHHII", 1, 512, 512, 262144, 262144)
        self.assertEqual(len(raw), 16)
        self.assertEqual(Emcore.decode_packetsize(raw),
                         {"cout": 512, "cin": 512, "dout": 262144, "din": 262144})

    def test_pool(self):
        raw = struct.pack("<IIII", 1, 0x08000000, 0x0BFC0000, 0)
        self.assertEqual(Emcore.decode_pool(raw), (0x08000000, 0x0BFC0000))


class TestAlignsplit(unittest.TestCase):
    """The three parts must tile the request exactly and keep the body aligned."""

    def test_partitions_exactly(self):
        for addr in (0, 1, 7, 16, 0x08000000, 0x08000003, 0x0800000F):
            for size in (0, 1, 15, 16, 17, 495, 496, 497, 4096, 100000):
                head, body, tail = Emcore._alignsplit(addr, size, 496, AES_BLOCK)
                self.assertEqual(head + body + tail, size,
                                 "addr=0x%X size=%d" % (addr, size))
                self.assertTrue(head >= 0 and body >= 0 and tail >= 0)
                if body:
                    self.assertEqual((addr + head) % AES_BLOCK, 0,
                                     "body must start 16-byte aligned")
                    self.assertEqual(body % AES_BLOCK, 0,
                                     "body length must be a multiple of 16")

    def test_small_transfer_is_all_head(self):
        self.assertEqual(Emcore._alignsplit(0x08000000, 100, 496, 16), (100, 0, 0))


class FakeAesDevice:
    """Stands in for the SoC: AES-128-CBC decrypt with IV forced to 0 per call,
    exactly like emCORE's hwkeyaes(), backed by a plain software AES."""

    def __init__(self, key, pool=(0x08000000, 0x0BFC0000)):
        self.key = key
        self.pool = pool
        self.mem = {}
        self.packetsize = {"cout": 512, "cin": 512, "dout": 262144, "din": 262144}
        self.calls = 0

    def malloc(self, size):
        return self.pool[0]

    def free(self, ptr):
        pass

    def write(self, addr, data):
        self.mem[addr] = bytearray(data)

    def read(self, addr, size):
        return bytes(self.mem[addr][:size])

    def aes(self, addr, size, keyidx, encrypt=False, timeout=None):
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
        self.calls += 1
        buf = bytes(self.mem[addr][:size])
        d = Cipher(algorithms.AES(self.key), modes.CBC(b"\0" * 16)).decryptor()
        self.mem[addr][:size] = d.update(buf) + d.finalize()


class TestChunkedCbcStitch(unittest.TestCase):
    """Chunked decryption + seam repair must equal a single contiguous decrypt.

    This is the property that makes it safe to stream a 9.9 MB firmware image
    through a 1 MiB device buffer.  It was also confirmed on real hardware.
    """

    def setUp(self):
        try:
            import cryptography  # noqa: F401
        except ImportError:
            self.skipTest("python3-cryptography not installed")

    def test_stitch_matches_contiguous(self):
        import os as _os
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
        key = _os.urandom(16)
        plain = _os.urandom(8 * 1024)
        enc = Cipher(algorithms.AES(key), modes.CBC(b"\0" * 16)).encryptor()
        ct = enc.update(plain) + enc.finalize()

        dev = FakeAesDevice(key)
        got = decrypt_payload(dev, ct, keyidx=KEY_GID, chunk=1024)
        self.assertGreater(dev.calls, 1, "test must actually exercise >1 chunk")
        self.assertEqual(got, plain)

    def test_unaligned_tail_passes_through(self):
        import os as _os
        from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
        key = _os.urandom(16)
        body = _os.urandom(1024)
        enc = Cipher(algorithms.AES(key), modes.CBC(b"\0" * 16)).encryptor()
        ct = enc.update(body) + enc.finalize() + b"\xde\xad\xbe\xef"  # 4 stray bytes
        dev = FakeAesDevice(key)
        got = decrypt_payload(dev, ct, keyidx=KEY_GID, chunk=512)
        self.assertEqual(len(got), len(ct))
        self.assertEqual(got[-4:], b"\xde\xad\xbe\xef")


if __name__ == "__main__":
    unittest.main(verbosity=2)
