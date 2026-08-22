import gdb,sys
CURTCB=0x2200ac64; TCB_BASE=0x0897ff70; STRIDE=52; SEMA=0x0897fb1c
def rd32(a):
    try: return int.from_bytes(gdb.selected_inferior().read_memory(a,4).tobytes(),"little")
    except Exception: return None
def rd8(a):
    try: return gdb.selected_inferior().read_memory(a,1).tobytes()[0]
    except Exception: return None
def reg(n): return int(gdb.parse_and_eval("$"+n))&0xffffffff
def P(*a): print(*a);sys.stdout.flush()
def code(a): return a is not None and (0x08000000<=a<0x08400000 or 0x22000000<=a<0x2200c000)
def audio(a): return a is not None and (0x08016000<=a<0x08018000 or 0x08080000<=a<0x0809c000 or 0x08090000<=a<0x08092000 or a==0x07ffb2d0)
gdb.execute("set pagination off");gdb.execute("set confirm off");gdb.execute("set target-async off")
gdb.execute("target remote :1234")
cur=rd32(CURTCB); curtn=rd32((cur or 0)+32)
P("=== attached pc=0x%08x cpsr=0x%08x  current task=0x%x ===" % (reg("pc"),reg("cpsr"),curtn or 0))
P("sema[6]=0x%08x" % (rd32(SEMA+6*4) or 0))
for i in range(0,0x40):
    tcb=TCB_BASE+i*STRIDE; tn=rd32(tcb+32)
    if tn!=i: continue
    entry=rd32(tcb+12); savedSP=rd32(tcb+16); state=rd8(tcb+28)
    rb=rd32((savedSP or 0)+4) if savedSP else 0
    wo=rd32((rb or 0)+8) if rb else None
    rpc=rd32((savedSP or 0)+56) if savedSP else 0
    # backtrace: scan stack for code addrs
    chain=[]; aud=False
    base=(savedSP or 0)+64
    for j in range(0,400,4):
        w=rd32(base+j)
        if code(w):
            chain.append(w)
            if audio(w): aud=True
        if len(chain)>=10: break
    flag=" <<<AUDIO" if aud else ("  CURRENT" if tcb==(cur or 0) else "")
    P("t%02x st=0x%02x entry=0x%08x sem=%s rpc=0x%08x%s" % (
        i, state or 0, entry or 0, ("0x%x"%wo) if wo is not None else "-", rpc or 0, flag))
    P("    bt: "+" ".join("%08x"%c for c in chain[:8]))
gdb.execute("detach");gdb.execute("quit")
