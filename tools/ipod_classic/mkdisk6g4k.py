#!/usr/bin/env python3
"""Build an iPod Classic 6G disk image whose MBR is written in 4096-byte units.

The retailOS locates the firmware partition in 0x0808f8f4: it reads ONE device
block at block 0, checks 0xAA55 at +0x1FE, walks the four MBR entries looking
for a type byte of 0x00 or 0x3F, and takes that entry's start/size *in device
blocks*.  The device block on an iPod Classic is 4096 bytes (measured: the
first DMA read of every boot is 8 x 512-byte sectors at LBA 0), so the LBA
fields in the partition table count 4096-byte blocks, not sectors.

    mkdisk6g4k.py <out> <firmware-partition-image> <fat32-image>
"""
import struct
import sys
import os

BS = 4096
out, fwp, fat = sys.argv[1], sys.argv[2], sys.argv[3]
fwb = open(fwp, 'rb').read()
fatb = open(fat, 'rb').read()


def pad(b):
    return b + b'\0' * (-len(b) % BS)


fwb, fatb = pad(fwb), pad(fatb)
fw_start = 8                       # block 8 = byte 0x8000
fw_blocks = len(fwb) // BS
fat_start = fw_start + fw_blocks
fat_blocks = len(fatb) // BS

mbr = bytearray(BS)


def ent(i, ptype, lba, cnt):
    off = 0x1be + 16 * i
    mbr[off + 4] = ptype
    mbr[off + 8:off + 12] = struct.pack('<I', lba)
    mbr[off + 12:off + 16] = struct.pack('<I', cnt)


ent(0, 0x00, fw_start, fw_blocks)
ent(1, 0x0b, fat_start, fat_blocks)
mbr[0x1fe:0x200] = b'\x55\xaa'

with open(out, 'wb') as f:
    f.write(bytes(mbr))
    f.write(b'\0' * (BS * (fw_start - 1)))
    f.write(fwb)
    f.write(fatb)

print("4K-unit MBR: fw block %d (+%d), fat block %d (+%d); %d MB"
      % (fw_start, fw_blocks, fat_start, fat_blocks,
         os.path.getsize(out) // (1024 * 1024)))
