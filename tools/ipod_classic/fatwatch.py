#!/usr/bin/env python3
"""fatwatch.py -- gdb read-watchpoints on the FAT boot-sector buffer while a
navigation thread drives the HMP monitor into Music.

QEMU launched with: -S -gdb tcp:GPORT -monitor tcp:MPORT,server,nowait
The data-partition boot sector lands at a stable guest buffer B; we watch a few
BPB offsets for reads and print the PC of whatever examines them.
"""
import argparse, socket, sys, os, threading, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gdbpoke import GdbRemote, fmt_regs

def monitor_nav(mport, log):
    try:
        m = socket.create_connection(('127.0.0.1', mport), timeout=10)
    except OSError as e:
        log('monitor connect failed: %s' % e); return
    m.settimeout(2.0)
    def send(cmd):
        try: m.sendall((cmd+'\n').encode())
        except OSError: pass
        log('MON> %s' % cmd)
    # timings mirror navtest/navmusic (wall clock from power-on)
    seq = [(48,'sendkey e 150'),(6,'sendkey e 150'),(6,'sendkey q 150'),
           (6,'sendkey ret 150'),(28,'sendkey e 150'),(8,'sendkey q 150'),
           (8,'sendkey ret 150'),(20,'screendump /mnt/c/Desarrollo/qemu-ios/build/fatwatch.ppm')]
    for dt,cmd in seq:
        time.sleep(dt); send(cmd)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--gport', type=int, default=1280)
    ap.add_argument('--mport', type=int, default=1281)
    ap.add_argument('--buf', type=lambda x:int(x,0), default=0x8be5d400)
    ap.add_argument('--hits', type=int, default=25)
    ap.add_argument('--timeout', type=float, default=200.0)
    a = ap.parse_args()

    def log(s): print(s, flush=True)

    g = GdbRemote('127.0.0.1', a.gport, a.timeout)
    log('gdb stub: %s' % g.cmd('?'))

    # Read watchpoints (Z3) on key BPB offsets, both uncached (0x8...) and
    # cached (0x0...) aliases of the stable buffer.
    base_unc = a.buf
    base_cac = a.buf & ~0x80000000
    offs = [(0x00,2),(0x0b,2),(0x0d,2),(0x10,2),(0x36,8),(0x52,8),(0x1fe,2)]
    for base in (base_unc, base_cac):
        for off,ln in offs:
            r = g.cmd('Z3,%x,%x' % (base+off, ln))
            log('Z3 0x%08x/%d -> %s' % (base+off, ln, r or '(ok)'))

    # start navigation
    t = threading.Thread(target=monitor_nav, args=(a.mport, log), daemon=True)
    t.start()

    for hit in range(a.hits):
        try:
            stop = g.cont()
        except socket.timeout:
            log('TIMEOUT after %d hits' % hit); break
        r = g.regs()
        pc = r[15] or 0
        # dump the boot sector via cached alias for readability
        d = g.mem(base_cac, 0x200)
        bps = d[0x0b] | (d[0x0c]<<8) if d else 0
        log('\n=== WATCH hit %d  stop=%s  pc=0x%08x ===' % (hit+1, stop, pc))
        log(fmt_regs(r))
        if d:
            log('  bootsec: jmp=%02x%02x%02x oem=%r fstype52=%r sig=%02x%02x bps=%d'
                % (d[0],d[1],d[2], bytes(d[3:11]), bytes(d[0x52:0x5a]) if len(d)>0x5a else b'',
                   d[0x1fe] if len(d)>0x1ff else 0, d[0x1ff] if len(d)>0x1ff else 0, bps))
    g.cmd('D'); g.close()

if __name__ == '__main__':
    sys.exit(main())
