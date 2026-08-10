#!/usr/bin/env python3
"""osana.py - analysis helpers for osos_decrypted.bin (iPod classic 6G retailOS).

Scatter-load layout (wiki/11-classic6g-retailos.md sec.1):
    file [0x00000,0x0AE48) -> 0x22000000
    file [0x0AE48, end)    -> 0x08000000
so for the DRAM body: addr = 0x08000000 + (off - 0xAE48), i.e. base 0x07FF51B8.

Subcommands:
  xref  <addr>   every BL/BLX-imm/B whose target is <addr>, plus every word
                 in the image equal to <addr> (vtable slot / pointer table)
  lit   <val>    every PC-relative LDR that loads the literal <val>
  word  <val>    every 4-byte-aligned word equal to <val>
  bytes <hex>    every occurrence of a raw byte string
  dis   <a> <b>  objdump range
"""
import sys, struct, subprocess, os

IMG = os.path.join(os.path.dirname(__file__), '..', '..',
                   'assets', 'classic6g', 'osos_decrypted.bin')
SPLIT = 0xAE48
IRAM_BASE = 0x22000000
DRAM_BASE = 0x08000000
DRAM_VMA = DRAM_BASE - SPLIT          # 0x07FF51B8


def load():
    with open(IMG, 'rb') as f:
        return f.read()


def off2addr(off):
    if off < SPLIT:
        return IRAM_BASE + off
    return DRAM_VMA + off


def addr2off(addr):
    if IRAM_BASE <= addr < IRAM_BASE + SPLIT:
        return addr - IRAM_BASE
    return addr - DRAM_VMA


def words(data):
    n = len(data) // 4
    return struct.unpack('<%dI' % n, data[:n * 4])


def cmd_word(data, val):
    for i, w in enumerate(words(data)):
        if w == val:
            print('word  %08x  (file %07x)' % (off2addr(i * 4), i * 4))


def cmd_xref(data, target):
    w = words(data)
    for i, insn in enumerate(w):
        off = i * 4
        pc = off2addr(off)
        cond = insn >> 28
        # B / BL  (cond 0..14)
        if cond != 0xF and (insn & 0x0E000000) == 0x0A000000:
            imm = insn & 0xFFFFFF
            if imm & 0x800000:
                imm -= 0x1000000
            dst = (pc + 8 + imm * 4) & 0xFFFFFFFF
            if dst == target:
                kind = 'BL' if (insn & 0x01000000) else 'B '
                print('%s   %08x -> %08x  (file %07x)' % (kind, pc, dst, off))
        # BLX immediate (unconditional, 0xFA/0xFB)
        elif cond == 0xF and (insn & 0x0E000000) == 0x0A000000:
            imm = insn & 0xFFFFFF
            if imm & 0x800000:
                imm -= 0x1000000
            h = (insn >> 24) & 1
            dst = (pc + 8 + imm * 4 + h * 2) & 0xFFFFFFFF
            if dst == target or dst == target + 1:
                print('BLX  %08x -> %08x  (file %07x)' % (pc, dst, off))
    print('--- data words equal to %08x ---' % target)
    cmd_word(data, target)


def cmd_lit(data, val):
    w = words(data)
    n = len(w)
    for i, insn in enumerate(w):
        if (insn & 0x0F7F0000) != 0x051F0000:      # LDR rD,[pc,#-imm]
            if (insn & 0x0F7F0000) != 0x059F0000:  # LDR rD,[pc,#+imm]
                continue
        up = (insn >> 23) & 1
        imm = insn & 0xFFF
        pc = off2addr(i * 4)
        ea = pc + 8 + (imm if up else -imm)
        j = addr2off(ea)
        if j % 4 or j < 0 or j // 4 >= n:
            continue
        if w[j // 4] == val:
            print('LDR  %08x  literal@%08x = %08x' % (pc, ea, val))


def main():
    data = load()
    cmd = sys.argv[1]
    if cmd == 'word':
        cmd_word(data, int(sys.argv[2], 0))
    elif cmd == 'xref':
        cmd_xref(data, int(sys.argv[2], 0))
    elif cmd == 'lit':
        cmd_lit(data, int(sys.argv[2], 0))
    elif cmd == 'bytes':
        pat = bytes.fromhex(sys.argv[2])
        i = data.find(pat)
        while i >= 0:
            print('%08x  (file %07x)' % (off2addr(i), i))
            i = data.find(pat, i + 1)
    elif cmd == 'dis':
        a, b = int(sys.argv[2], 0), int(sys.argv[3], 0)
        vma = IRAM_BASE if a < DRAM_BASE + 0x10000000 and a >= IRAM_BASE else DRAM_VMA
        subprocess.call(['arm-none-eabi-objdump', '-D', '-b', 'binary', '-m', 'arm',
                         '--adjust-vma=0x%x' % vma,
                         '--start-address=0x%x' % a, '--stop-address=0x%x' % b, IMG])
    else:
        print(__doc__)


if __name__ == '__main__':
    main()
