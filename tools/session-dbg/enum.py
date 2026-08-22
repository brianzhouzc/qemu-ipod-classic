# Attach (halts), report PC + spin-check, enumerate RTXC task table from memory.
import gdb, sys
CURTCB=0x2200ac64; TCB_BASE=0x0897ff70; STRIDE=52; SEMA=0x0897fb1c
def rd32(a):
    try: return int.from_bytes(gdb.selected_inferior().read_memory(a,4).tobytes(),"little")
    except Exception: return None
def rd8(a):
    try: return gdb.selected_inferior().read_memory(a,1).tobytes()[0]
    except Exception: return None
def reg(n): return int(gdb.parse_and_eval("$"+n)) & 0xffffffff
def P(*a): print(*a); sys.stdout.flush()
def enum():
    cur=rd32(CURTCB)
    P("current TCB=0x%08x tasknum=0x%x" % (cur or 0, rd32((cur or 0)+32) or 0))
    P("id state entry      savedSP    resumePC   waitObj  (0x40=WAITING)")
    for i in range(0,64):
        tcb=TCB_BASE+i*STRIDE
        entry=rd32(tcb+12); savedSP=rd32(tcb+16); state=rd8(tcb+28); tn=rd32(tcb+32)
        if tn!=i: continue
        rb=rd32((savedSP or 0)+4) if savedSP else 0
        wo=rd32((rb or 0)+8) if rb else None
        rpc=rd32((savedSP or 0)+56) if savedSP else 0
        P("%02x  0x%02x 0x%08x 0x%08x 0x%08x %s%s" % (i,state or 0,entry or 0,savedSP or 0,
          rpc or 0, ("sem=0x%x"%wo) if wo is not None else "-",
          "  <== WAITING" if state==0x40 else ("  <== CURRENT/READY" if tcb==(cur or 0) else "")))
    P("--- sema array id:val (FFFFFFFF=empty 0=done else=waiter) ---")
    s=""
    for i in range(0,48):
        v=rd32(SEMA+i*4); s+="%02x:%08x " % (i,v if v is not None else 0)
        if i%4==3: P("  "+s); s=""
gdb.execute("set pagination off"); gdb.execute("set confirm off")
gdb.execute("set target-async off")
gdb.execute("target remote :1234")
pc0=reg("pc"); lr=reg("lr"); sp=reg("sp"); cpsr=reg("cpsr")
P("=== attached: pc=0x%08x lr=0x%08x sp=0x%08x cpsr=0x%08x ===" % (pc0,lr,sp,cpsr))
# spin check: stepi 12 times, collect distinct PCs
P("stepi trace:")
pcs=[]
for k in range(12):
    gdb.execute("stepi", to_string=True)
    pcs.append(reg("pc"))
P("  "+" ".join("%08x"%p for p in pcs))
lo=min(pcs); hi=max(pcs)
P("  span 0x%08x..0x%08x %s" % (lo,hi,"(TIGHT SPIN)" if (hi-lo)<0x40 else "(progressing)"))
enum()
gdb.execute("detach"); gdb.execute("quit")
