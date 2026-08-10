#!/usr/bin/env python3
"""Single-step tracer: break at ADDR, then step N times, print block ranges."""
import sys, argparse
sys.path.insert(0, "/mnt/c/Desarrollo/qemu-ios/tools/ipod_classic")
from gdbpoke import GdbRemote, fmt_regs

ap = argparse.ArgumentParser()
ap.add_argument("--port", type=int, default=1400)
ap.add_argument("--break", dest="breaks", action="append", default=[])
ap.add_argument("--skiphits", type=int, default=0, help="ignore first N hits")
ap.add_argument("--steps", type=int, default=5000)
ap.add_argument("--timeout", type=float, default=600.0)
ap.add_argument("--regs-at", action="append", default=[], help="print regs when pc==X")
a = ap.parse_args()
regs_at = set(int(x,0) for x in a.regs_at)

g = GdbRemote("127.0.0.1", a.port, a.timeout)
print("stub:", g.cmd("?"))
for b in a.breaks:
    g.set_break(int(b, 0)); print("bp", b)
for i in range(a.skiphits + 1):
    g.cont()
r = g.regs()
print("=== stop pc=%08x ===" % r[15]); print(fmt_regs(r))

seq = []
for i in range(a.steps):
    g.step()
    pc = g.reg(15)
    seq.append(pc)
    if pc in regs_at:
        print("!! pc=%08x regs:" % pc); print(fmt_regs(g.regs()))

# compress to ranges of straight-line execution
out = []
start = prev = None
for pc in seq:
    if prev is not None and pc == prev + 4:
        prev = pc; continue
    if start is not None:
        out.append((start, prev))
    start = prev = pc
if start is not None:
    out.append((start, prev))
print("--- %d steps, %d blocks ---" % (len(seq), len(out)))
# collapse repeated identical consecutive blocks
i = 0
while i < len(out):
    j = i
    while j+1 < len(out) and out[j+1] == out[i]:
        j += 1
    n = j - i + 1
    s,e = out[i]
    print("%08x..%08x%s" % (s, e, ("  x%d" % n) if n>1 else ""))
    i = j+1
g.cmd("D"); g.close()
