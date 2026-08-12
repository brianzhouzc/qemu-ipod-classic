#!/usr/bin/env python3
"""fatprobe.py -- drive QEMU's gdbstub to inspect the FAT mount path.

Reuses GdbRemote from gdbpoke.py. Unlike gdbpoke, at each breakpoint it can
dump memory relative to a register, e.g. --dump r4:0:0x40 dumps [r4+0..0x40).

    python3 fatprobe.py --port 1234 --timeout 120 \
        --bp 0x0827a980:entry \
        --bp 0x0827a9b0:afterread:r4:0:0x40:r4:0x1f0:0x10 \
        --hits 40

Each --bp is ADDR:label[:reg:off:len ...]. On a stop at ADDR it prints the
label, all regs, and each requested dump.
"""
import argparse
import socket
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gdbpoke import GdbRemote, fmt_regs

REGNAMES = ['r0','r1','r2','r3','r4','r5','r6','r7','r8','r9','r10','r11',
            'r12','sp','lr','pc','sl','fp','ip']

def regidx(name):
    m = {'sl':10,'fp':11,'ip':12,'sp':13,'lr':14,'pc':15}
    if name in m: return m[name]
    if name.startswith('r'): return int(name[1:])
    raise ValueError(name)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, default=1234)
    ap.add_argument('--bp', action='append', default=[])
    ap.add_argument('--hits', type=int, default=20)
    ap.add_argument('--timeout', type=float, default=120.0)
    a = ap.parse_args()

    bps = {}
    for spec in a.bp:
        parts = spec.split(':')
        addr = int(parts[0], 0)
        label = parts[1] if len(parts) > 1 else ''
        dumps = []
        rest = parts[2:]
        for i in range(0, len(rest), 3):
            dumps.append((rest[i], int(rest[i+1],0), int(rest[i+2],0)))
        bps[addr] = (label, dumps)

    g = GdbRemote(a.host, a.port, a.timeout)
    print('connected; stub says: %s' % g.cmd('?'))
    for addr in bps:
        g.set_break(addr)
        print('bp 0x%08x %s' % (addr, bps[addr][0]))

    for hit in range(a.hits):
        try:
            stop = g.cont()
        except socket.timeout:
            print('TIMEOUT after %d hits' % hit)
            break
        r = g.regs()
        pc = r[15] or 0
        label, dumps = bps.get(pc, ('?', []))
        print('\n=== hit %d  pc=%08x  [%s] ===' % (hit+1, pc, label))
        print(fmt_regs(r))
        for reg, off, ln in dumps:
            base = r[regidx(reg)] or 0
            addr = base + off
            d = g.mem(addr, ln)
            if d is None:
                print('  %s+0x%x (=%08x): <fault>' % (reg, off, addr))
            else:
                for o in range(0, len(d), 16):
                    chunk = d[o:o+16]
                    hexs = ' '.join('%02x' % b for b in chunk)
                    asc = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
                    print('  %08x: %-47s  %s' % (addr+o, hexs, asc))
    g.cmd('D')
    g.close()

if __name__ == '__main__':
    sys.exit(main())
