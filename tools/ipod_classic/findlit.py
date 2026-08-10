#!/usr/bin/env python3
"""findlit.py -- where a 32-bit literal sits in osos_decrypted.bin, in guest VA.

    python3 findlit.py <image> <hexword> [<hexword>...]

The image is scatter-loaded (wiki/11 sec.1): file [0,0xAE48) -> 0x22000000 and
file [0xAE48,end) -> 0x08000000. Prints the VA of every little-endian
occurrence, plus every PC-relative LDR in the DRAM body that resolves to it.
"""
import struct
import sys

IRAM_END = 0xAE48


def va(off):
    if off < IRAM_END:
        return 0x22000000 + off
    return 0x08000000 + (off - IRAM_END)


def main():
    data = open(sys.argv[1], 'rb').read()
    wants = [int(a, 16) for a in sys.argv[2:]]
    pool = {}
    for w in wants:
        needle = struct.pack('<I', w)
        i = 0
        hits = []
        while True:
            i = data.find(needle, i)
            if i < 0:
                break
            if i % 4 == 0:
                hits.append(va(i))
                pool[va(i)] = w
            i += 4
        print('literal 0x%08x: %d aligned occurrences' % (w, len(hits)))
        for h in hits:
            print('    at 0x%08x' % h)

    # PC-relative LDRs in the DRAM body that land on one of those words.
    print('--- ldr rX,[pc,#imm] resolving to one of them ---')
    for off in range(IRAM_END, len(data) - 4, 4):
        word = struct.unpack_from('<I', data, off)[0]
        if (word & 0x0F7F0000) != 0x051F0000 and (word & 0x0F7F0000) != 0x059F0000:
            continue
        imm = word & 0xFFF
        up = (word >> 23) & 1
        pc = va(off) + 8
        tgt = pc + imm if up else pc - imm
        if tgt in pool:
            print('    0x%08x  ldr r%d, =0x%08x' % (va(off), (word >> 12) & 0xF,
                                                    pool[tgt]))


if __name__ == '__main__':
    main()
