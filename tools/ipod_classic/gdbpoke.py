#!/usr/bin/env python3
"""gdbpoke.py -- a 200-line GDB remote client for QEMU's built-in stub.

Why this exists: this WSL has no gdb-multiarch, and the question that keeps
coming up ("what is in r0 when the guest reaches address X?") cannot be
answered with -d in_asm, which only prints translation-block heads.

Run QEMU with -s -S (or -gdb tcp::PORT -S) and then:

    python3 gdbpoke.py --port 1234 \\
        --break 0x0815b1dc --break 0x0815b1f0 \\
        --hits 8 --timeout 120

It sets the breakpoints, continues, and prints r0..r15 + cpsr at every stop,
plus a few words of memory if --mem is given.  --step N single-steps N times
after the last stop.  --mem ADDR:LEN dumps memory at each stop.

Nothing here is iPod-specific except the defaults.
"""
import argparse
import socket
import sys
import time


class GdbRemote:
    def __init__(self, host, port, timeout=30.0):
        self.s = socket.create_connection((host, port), timeout=10.0)
        self.s.settimeout(timeout)
        self.buf = b''

    def close(self):
        try:
            self.s.close()
        except OSError:
            pass

    def _send(self, data):
        csum = sum(data.encode()) & 0xff
        pkt = b'$' + data.encode() + b'#' + ('%02x' % csum).encode()
        self.s.sendall(pkt)

    def _recv_raw(self, n=4096):
        d = self.s.recv(n)
        if not d:
            raise EOFError('gdbstub closed the connection')
        return d

    def _read_packet(self):
        """Return the payload of the next $...# packet, ACKing it."""
        while True:
            i = self.buf.find(b'$')
            if i >= 0:
                j = self.buf.find(b'#', i)
                if j >= 0 and len(self.buf) >= j + 3:
                    payload = self.buf[i + 1:j]
                    self.buf = self.buf[j + 3:]
                    self.s.sendall(b'+')
                    return payload.decode('latin1')
            self.buf += self._recv_raw()

    def cmd(self, data):
        self._send(data)
        # Swallow the stub's ACK if it sends one.
        deadline = time.time() + 5.0
        while b'+' in self.buf[:1] or b'-' in self.buf[:1]:
            self.buf = self.buf[1:]
        while not self.buf and time.time() < deadline:
            self.buf += self._recv_raw()
        while self.buf[:1] in (b'+', b'-'):
            self.buf = self.buf[1:]
            if not self.buf:
                self.buf += self._recv_raw()
        return self._read_packet()

    # ---- convenience -------------------------------------------------
    def reg(self, n):
        r = self.cmd('p%x' % n)
        if r.startswith('E') or not r:
            return None
        return int.from_bytes(bytes.fromhex(r), 'little')

    def regs(self):
        """r0..r15 then cpsr.

        QEMU's ARM gdbstub keeps the legacy FPA hole: registers 16..23 are
        dummy FPA registers, 24 is a dummy FPSCR and CPSR is number 25
        (target/arm/gdbstub.c). Reading 16 and calling it cpsr gives zero.
        """
        return [self.reg(i) for i in range(16)] + [self.reg(25)]

    def mem(self, addr, length):
        r = self.cmd('m%x,%x' % (addr, length))
        if r.startswith('E'):
            return None
        return bytes.fromhex(r)

    def set_break(self, addr):
        return self.cmd('Z0,%x,4' % addr)

    def set_reg(self, n, value):
        """P<n>=<little-endian hex>. Used to jump over a call."""
        return self.cmd('P%x=%s' % (n, value.to_bytes(4, 'little').hex()))

    def cont(self):
        """Resume, stepping over a breakpoint at the current PC first.

        QEMU's TCG breakpoint check (accel/tcg/cpu-exec.c:346) fires whenever
        pc == bp->pc with no "we just came from here" exemption, so a bare 'c'
        from a stop re-reports the same breakpoint for ever. The comment three
        lines above it says single-step overrides breakpoints, which is the
        escape: step one instruction, then continue. Real GDB removes and
        reinserts instead; this is cheaper and needs no bookkeeping.
        """
        self._send('s')
        self._read_packet()
        self._send('c')
        return self._read_packet()

    def step(self):
        self._send('s')
        return self._read_packet()


def fmt_regs(r):
    names = ['r0', 'r1', 'r2', 'r3', 'r4', 'r5', 'r6', 'r7',
             'r8', 'r9', 'sl', 'fp', 'ip', 'sp', 'lr', 'pc', 'cpsr']
    out = []
    for i, n in enumerate(names):
        v = r[i] if i < len(r) and r[i] is not None else 0
        out.append('%-4s %08x' % (n, v))
    return ('\n'.join('  '.join(out[i:i + 4]) for i in range(0, len(out), 4)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, default=1234)
    ap.add_argument('--break', dest='breaks', action='append', default=[])
    ap.add_argument('--mem', action='append', default=[],
                    help='ADDR:LEN, dumped at every stop')
    ap.add_argument('--hits', type=int, default=1)
    ap.add_argument('--step', type=int, default=0)
    ap.add_argument('--timeout', type=float, default=120.0)
    ap.add_argument('--skip', action='append', default=[],
                    help='FROM=TO[:R0] -- when the guest stops at FROM, set pc '
                         'to TO (and optionally r0) and carry on. Lets you jump '
                         'over a call without touching the guest image.')
    ap.add_argument('--quiet-skips', action='store_true',
                    help='do not print a line for each skip')
    ap.add_argument('--skip-limit', type=int, default=0,
                    help='fire each --skip at most N times (0 = no limit)')
    a = ap.parse_args()

    skips = {}
    fired = {}
    for s in a.skip:
        src, _, rest = s.partition('=')
        dst, _, r0 = rest.partition(':')
        skips[int(src, 0)] = (int(dst, 0), int(r0, 0) if r0 else None)
        fired[int(src, 0)] = 0

    g = GdbRemote(a.host, a.port, a.timeout)
    print('connected; stub says: %s' % g.cmd('?'))
    for b in a.breaks:
        addr = int(b, 0)
        print('break 0x%08x -> %s' % (addr, g.set_break(addr) or '(empty)'))
    for addr in skips:
        print('break 0x%08x (skip) -> %s' % (addr, g.set_break(addr) or '(empty)'))

    nskip = 0
    for hit in range(a.hits):
        try:
            stop = g.cont()
        except socket.timeout:
            print('TIMEOUT waiting for a stop after %d hit(s), %d skip(s)'
                  % (hit, nskip))
            break
        pc = g.reg(15)
        if pc in skips and (a.skip_limit == 0 or fired[pc] < a.skip_limit):
            fired[pc] += 1
            dst, r0 = skips[pc]
            if r0 is not None:
                g.set_reg(0, r0)
            g.set_reg(15, dst)
            nskip += 1
            if not a.quiet_skips:
                print('skip %d: pc %08x -> %08x' % (nskip, pc, dst))
            continue
        r = g.regs()
        print('\n=== stop %d (%s)  pc=%08x ===' % (hit + 1, stop, r[15] or 0))
        print(fmt_regs(r))
        for m in a.mem:
            addr, _, ln = m.partition(':')
            addr = int(addr, 0)
            ln = int(ln or '16', 0)
            d = g.mem(addr, ln)
            if d is None:
                print('  mem %08x: <fault>' % addr)
            else:
                for off in range(0, len(d), 16):
                    chunk = d[off:off + 16]
                    print('  %08x: %s' % (addr + off,
                          ' '.join('%08x' % int.from_bytes(chunk[i:i + 4], 'little')
                                   for i in range(0, len(chunk), 4))))
    for i in range(a.step):
        stop = g.step()
        r = g.regs()
        print('\n--- step %d (%s) pc=%08x ---' % (i + 1, stop, r[15] or 0))
        print(fmt_regs(r))

    g.cmd('D')
    g.close()


if __name__ == '__main__':
    sys.exit(main())
