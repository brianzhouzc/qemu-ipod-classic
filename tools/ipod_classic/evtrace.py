#!/usr/bin/env python3
"""evtrace.py -- press a key on the running retailOS and see where it goes.

gdbrun.sh can set breakpoints but cannot press anything: it starts QEMU with
-monitor none and the gdbstub owns the run.  This one owns both ends.  It
launches QEMU with a gdbstub AND a monitor on a unix socket, lets the machine
boot free-running, then attaches, arms the breakpoints, and only then presses
the key -- from a thread, so the press lands while the guest is running.

    python3 evtrace.py --secs 120 --boot 55 --key w \
        --break 0x080ea82c --break 0x080ea514 --hits 6

Every stop prints r0..r15+cpsr, so "did the queue get drained" is a question
with an answer instead of an opinion.

--steps N single-steps N instructions after the LAST breakpoint hit and prints
the straight-line blocks, which is how you follow a handler into whatever it
decides to call.

Nothing here writes to guest memory: it is a measuring instrument.
"""
import argparse
import os
import socket
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gdbpoke import GdbRemote, fmt_regs      # noqa: E402

ROOT = '/mnt/c/Desarrollo/qemu-ios'


def monitor_send(sock_path, lines, delay):
    time.sleep(delay)
    try:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(sock_path)
        time.sleep(0.3)
        for ln in lines:
            if ln.startswith('wait '):
                time.sleep(float(ln.split()[1]))
                continue
            s.sendall((ln + '\n').encode())
            time.sleep(0.3)
        time.sleep(0.5)
        s.close()
        print('[monitor] sent: %s' % '; '.join(lines), flush=True)
    except OSError as e:
        print('[monitor] FAILED: %s' % e, flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--port', type=int, default=1500)
    ap.add_argument('--secs', type=int, default=180, help='qemu wall-clock cap')
    ap.add_argument('--boot', type=int, default=55, help='free-run before attach')
    ap.add_argument('--disk', default=os.path.expanduser('~/6g/E.img'))
    ap.add_argument('--key', action='append', default=[],
                    help='monitor sendkey argument, e.g. "w 150"')
    ap.add_argument('--seq', default=None,
                    help='semicolon-separated monitor script, overrides --key '
                         'and --moncmd. "wait N" sleeps N seconds. e.g. '
                         '"sendkey w 150; wait 25; sendkey ret 150"')
    ap.add_argument('--moncmd', action='append', default=[],
                    help='raw monitor command, sent after the --key ones')
    ap.add_argument('--keydelay', type=float, default=4.0,
                    help='seconds after the first continue before the key')
    ap.add_argument('--break', dest='breaks', action='append', default=[])
    ap.add_argument('--hits', type=int, default=4)
    ap.add_argument('--steps', type=int, default=0)
    ap.add_argument('--mem', action='append', default=[])
    ap.add_argument('--word', dest='words', action='append', default=[],
                    help='SPEC = reg.off[.off]* -- print the words along that '
                         'pointer chain, in hex')
    ap.add_argument('--str', dest='strs', action='append', default=[],
                    help='SPEC = reg[.deref]* -- follow the chain from that '
                         'register and print the NUL-terminated string there. '
                         '"r1.0" means *(r1+0) then the string at that address')
    ap.add_argument('--timeout', type=float, default=90.0)
    ap.add_argument('--err', default=None)
    ap.add_argument('--endshot', default=None,
                    help='screendump here after detaching, to prove the guest '
                         'was still alive and on the expected screen')
    # A private COPY of build/qemu-system-arm, under a different name. Two
    # reasons, both learned the hard way: another session's
    # "pkill -9 -x qemu-system-arm" kills a run under a name it matches, and a
    # concurrent "ninja" swaps the binary out from under a long measurement.
    ap.add_argument('--qemu', default=os.path.expanduser('~/evt/qemu-evt'))
    a = ap.parse_args()

    sock = '/tmp/evtrace.%d.mon' % a.port
    err = a.err or (os.path.expanduser('~/6g/evtrace.%d.err' % a.port))
    for p in (sock,):
        try:
            os.unlink(p)
        except OSError:
            pass
    cmd = ['timeout', str(a.secs), a.qemu, '-M', 'iPod-Classic',
           '-kernel', ROOT + '/assets/classic6g/osos_decrypted.bin',
           '-drive', 'if=ide,format=raw,file=%s,file.locking=off' % a.disk,
           '-snapshot', '-display', 'none', '-serial', 'null',
           '-monitor', 'unix:%s,server,nowait' % sock]
    fe = open(err, 'wb')
    proc = subprocess.Popen(cmd, cwd=os.path.dirname(a.qemu) or '.',
                            stdout=subprocess.DEVNULL, stderr=fe)
    print('[qemu] pid %d, booting %d s...' % (proc.pid, a.boot), flush=True)
    time.sleep(a.boot)

    # The gdbstub is turned on LATE, from the monitor, and this is not a
    # nicety: booting with "-gdb tcp::PORT" on the command line makes the
    # retailOS ask for a software reset (SWRCON = 0xaa5) somewhere between
    # 30 s and 55 s -- measured, twice, against two clean runs without it.
    # Whatever the stub costs, the guest notices it. Booting at full speed
    # and arming the stub once the language screen is up avoids the whole
    # question.
    monitor_send(sock, ['gdbserver tcp::%d' % a.port], 0)
    time.sleep(1)
    g = GdbRemote('127.0.0.1', a.port, a.timeout)
    print('attached; stub says: %s' % g.cmd('?'), flush=True)
    for b in a.breaks:
        addr = int(b, 0)
        print('break 0x%08x -> %s' % (addr, g.set_break(addr) or '(empty)'))

    lines = list(a.moncmd) + ['sendkey ' + k for k in a.key]
    if a.seq:
        lines = [x.strip() for x in a.seq.split(';') if x.strip()]
    if lines:
        threading.Thread(target=monitor_send,
                         args=(sock, lines, a.keydelay), daemon=True).start()

    for hit in range(a.hits):
        try:
            stop = g.cont()
        except socket.timeout:
            print('\nTIMEOUT after %d hit(s) -- nothing else stopped' % hit,
                  flush=True)
            break
        r = g.regs()
        print('\n=== stop %d (%s) pc=%08x  t=%.1fs ==='
              % (hit + 1, stop, r[15] or 0, time.time() % 1000), flush=True)
        print(fmt_regs(r))
        for m in a.mem:
            addr, _, ln = m.partition(':')
            addr = int(addr, 0)
            ln = int(ln or '16', 0)
            dmp = g.mem(addr, ln)
            print('  mem %08x: %s' % (addr, dmp.hex() if dmp else '<fault>'))
        for spec in a.words:
            parts = spec.split('.')
            p = r[int(parts[0][1:])] if parts[0][0] == 'r' else int(parts[0], 0)
            vals = []
            for d in parts[1:]:
                w = g.mem((p + int(d, 0)) & 0xffffffff, 4)
                if not w:
                    p = None
                    break
                p = int.from_bytes(w, 'little')
                vals.append('%08x' % p)
            print('  word %s: %s' % (spec, ' -> '.join(vals) or '<none>'))
        for spec in a.strs:
            parts = spec.split('.')
            p = r[int(parts[0][1:])] if parts[0][0] == 'r' else int(parts[0], 0)
            ok = True
            for d in parts[1:]:
                w = g.mem((p + int(d, 0)) & 0xffffffff, 4)
                if not w:
                    ok = False
                    break
                p = int.from_bytes(w, 'little')
            if not ok or p is None:
                print('  str %s: <fault>' % spec)
                continue
            blob = g.mem(p, 64)
            if blob is None:
                print('  str %s (0x%08x): <fault>' % (spec, p))
            else:
                txt = blob.split(b'\0')[0].decode('latin1')
                print('  str %s (0x%08x): %r' % (spec, p, txt))

    if a.steps:
        seq = []
        for _ in range(a.steps):
            g.step()
            seq.append(g.reg(15))
        out = []
        start = prev = None
        for pc in seq:
            if prev is not None and pc == prev + 4:
                prev = pc
                continue
            if start is not None:
                out.append((start, prev))
            start = prev = pc
        if start is not None:
            out.append((start, prev))
        print('--- %d steps, %d blocks ---' % (len(seq), len(out)))
        i = 0
        while i < len(out):
            j = i
            while j + 1 < len(out) and out[j + 1] == out[i]:
                j += 1
            n = j - i + 1
            s, e = out[i]
            print('%08x..%08x%s' % (s, e, ('  x%d' % n) if n > 1 else ''))
            i = j + 1

    try:
        g.cmd('D')
        g.close()
    except OSError:
        pass
    if a.endshot:
        # Proof that the run was still on its feet when the key was pressed.
        # The guest DOES sometimes software-reset during a gdb session; a run
        # whose last frame is black measured nothing and has to be thrown away.
        monitor_send(sock, ['cont', 'screendump ' + a.endshot], 0.5)
        time.sleep(2)
    proc.kill()
    fe.close()
    print('\n--- qemu stderr (first 10) ---')
    with open(err) as f:
        for i, ln in enumerate(f):
            if i >= 10:
                break
            print(ln.rstrip())


if __name__ == '__main__':
    sys.exit(main())
