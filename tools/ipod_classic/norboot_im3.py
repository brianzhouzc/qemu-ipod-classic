#!/usr/bin/env python3
"""norboot_im3.py -- identify and decrypt the IM3 image at NOR offset 0x8000.

The image at NOR 0x8000 is the *NOR boot image* of the S5L8702: Rockbox's
firmware/target/arm/s5l8702/norboot-target.h:81 defines NORBOOT_OFF 0x8000 and
its memory map comment shows that slot holding "Original NOR Boot" with an IM3
header at +0x000 and the payload at +0x800.

Header layout is `struct Im3Info`, crypto-s5l8702.h:50-70 (NOT the layout that
fwtool.py assumes, which is only right for enc_type 3/4):

    0x00 ident[4]      "8702"
    0x04 version[3]    "1.0"
    0x07 enc_type
    0x08 entry[4]      LE entry point
    0x0C data_sz[4]    LE payload size
    0x10 union:
           enc_type 1/2:  data_sign[16] ; _reserved[32]
           enc_type 3/4:  sign_off[4] cert_off[4] cert_sz[4] ; _reserved[36]
    0x40 info_sign[16]

Signatures are `im3_sign` (crypto-s5l8702.c:117-123):

    sign = AES_encrypt(keyidx, SHA1(data)[0:16])     AES-128-CBC, IV = 0

That is a *checkable* predicate, so we do not have to guess which fused key was
used: we compute SHA1 locally, ask the device to encrypt the truncated hash
under each key index, and compare against the stored signature.  Whichever key
reproduces info_sign is the key the image was signed with -- and, per
im3_read/im3_crypt (crypto-s5l8702.c:126-133), the same key encrypts the body.

Read-only with respect to the device: the only writes go to the emCORE malloc
pool in DRAM (see emcore_s5l8702.Emcore.write, which refuses anything else).
No bootflash write, no NAND, no execimage.
"""

import hashlib
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emcore_s5l8702 import Emcore, decrypt_payload, KEY_GID, KEY_UID  # noqa: E402

IM3HDR_SZ = 0x800        # crypto-s5l8702.h:38
IM3INFOSIGN_SZ = 0x40    # offsetof(struct Im3Info, info_sign)
SIGN_SZ = 16             # crypto-s5l8702.h:44
NORBOOT_OFF = 0x8000     # norboot-target.h:81

KEY_NAMES = {KEY_GID: "GKEY (per-model / GID)", KEY_UID: "UKEY (per-device)"}


def parse_info(hdr):
    ident = hdr[0:4]
    version = hdr[4:7]
    enc_type = hdr[7]
    entry, data_sz = struct.unpack("<II", hdr[8:0x10])
    info = {
        "ident": ident, "version": version, "enc_type": enc_type,
        "entry": entry, "data_sz": data_sz,
        "info_sign": hdr[0x40:0x40 + SIGN_SZ],
    }
    if enc_type in (1, 2):
        info["data_sign"] = hdr[0x10:0x10 + SIGN_SZ]
        info["reserved"] = hdr[0x20:0x40]
    else:
        info["sign_off"], info["cert_off"], info["cert_sz"] = \
            struct.unpack("<III", hdr[0x10:0x1C])
        info["reserved"] = hdr[0x1C:0x40]
    return info


def hw_encrypt(dev, data, keyidx):
    """AES-128-CBC encrypt `data` (multiple of 16) with a fused key."""
    buf = dev.malloc(max(len(data), 16))
    try:
        dev.write(buf, data)
        dev.aes(buf, len(data), keyidx, encrypt=True)
        return dev.read(buf, len(data))
    finally:
        dev.free(buf)


def identify_key(dev, blob, expected_sign, label):
    """Return the keyidx whose im3_sign(blob) == expected_sign, or None."""
    truncated = hashlib.sha1(blob).digest()[:SIGN_SZ]
    print("  SHA1(%s)[0:16] = %s" % (label, truncated.hex()))
    print("  stored signature = %s" % expected_sign.hex())
    for keyidx in (KEY_GID, KEY_UID):
        got = hw_encrypt(dev, truncated, keyidx)
        ok = got == expected_sign
        print("  AES_enc(key=%d %-22s) = %s  %s"
              % (keyidx, KEY_NAMES[keyidx], got.hex(), "MATCH" if ok else "no"))
        if ok:
            return keyidx
    return None


def main():
    nor_path = sys.argv[1] if len(sys.argv) > 1 else "assets/classic6g/bootflash_nor_1MB.bin"
    outdir = sys.argv[2] if len(sys.argv) > 2 else "assets/classic6g"
    offset = int(sys.argv[3], 0) if len(sys.argv) > 3 else NORBOOT_OFF

    nor = open(nor_path, "rb").read()
    hdr = nor[offset:offset + IM3HDR_SZ]
    info = parse_info(hdr)
    print("IM3 at NOR 0x%06X: ident=%r version=%r enc_type=0x%02x entry=0x%08X "
          "data_sz=0x%X (%d)" % (offset, info["ident"], info["version"],
                                 info["enc_type"], info["entry"],
                                 info["data_sz"], info["data_sz"]))
    if info["ident"] != b"8702":
        raise SystemExit("not an 8702 IM3 image")

    body = nor[offset + IM3HDR_SZ: offset + IM3HDR_SZ + info["data_sz"]]
    print("body: %d bytes from file offset 0x%X" % (len(body), offset + IM3HDR_SZ))

    dev = Emcore()
    print(dev.describe())

    print("\n[1] which key signed the header?  (im3_sign over Im3Info[0:0x40])")
    hkey = identify_key(dev, hdr[:IM3INFOSIGN_SZ], info["info_sign"], "header[0:0x40]")
    print("  -> header key: %s" % (KEY_NAMES.get(hkey, "NEITHER")))

    if "data_sign" in info:
        print("\n[2] which key signed the *ciphertext*?  (enc_type 1/2 data_sign)")
        dkey_ct = identify_key(dev, body, info["data_sign"], "ciphertext body")
        print("  -> ciphertext key: %s" % (KEY_NAMES.get(dkey_ct, "NEITHER")))

    print("\n[3] decrypting body under both keys")
    results = {}
    for keyidx in (KEY_GID, KEY_UID):
        pt = decrypt_payload(dev, body, keyidx=keyidx)
        results[keyidx] = pt
        path = os.path.join(outdir, "norboot_0x%X_key%d.bin" % (offset, keyidx))
        open(path, "wb").write(pt)
        print("  key %d -> %s" % (keyidx, path))
        if "data_sign" in info:
            sig = hw_encrypt(dev, hashlib.sha1(pt).digest()[:SIGN_SZ], keyidx)
            print("     im3_sign(plaintext, key=%d) = %s  %s"
                  % (keyidx, sig.hex(),
                     "MATCHES data_sign" if sig == info["data_sign"] else ""))


if __name__ == "__main__":
    main()
