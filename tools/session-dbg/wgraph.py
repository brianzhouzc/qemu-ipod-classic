# Wait-graph enumerator for RTXC/MeCCA on S5L8702 retailOS.
# Attaches to a STUCK machine (no continue), enumerates 56 tasks, and discovers
# every wait-queue head by scanning kernel data + IRAM for TCB pointers.
import gdb, sys
CURTCB   = 0x2200ac64
TCB_BASE = 0x0897ff70
STRIDE   = 52
NTASK    = 64
TCB_END  = TCB_BASE + NTASK*STRIDE       # 0x08980c70
SEMA     = 0x0897fb1c
MBOX     = 0x0897783c                     # mailbox table, stride 32
MBOX_STRIDE = 32

def rd32(a):
    try: return int.from_bytes(gdb.selected_inferior().read_memory(a,4).tobytes(),"little")
    except Exception: return None
def rd8(a):
    try: return gdb.selected_inferior().read_memory(a,1).tobytes()[0]
    except Exception: return None
def rdblk(a,n):
    try: return gdb.selected_inferior().read_memory(a,n).tobytes()
    except Exception: return None
def reg(n): return int(gdb.parse_and_eval("$"+n))&0xffffffff
def P(*a): print(*a); sys.stdout.flush()

def is_tcb_ptr(v):
    return v is not None and TCB_BASE<=v<TCB_END and ((v-TCB_BASE)%STRIDE)==0
def tid(v): return (v-TCB_BASE)//STRIDE
def code(a): return a is not None and (0x08000000<=a<0x08400000 or 0x22000000<=a<0x2200c000)

def rdstr(a):
    b=rdblk(a,24)
    if not b: return None
    s=b.split(b"\x00")[0]
    if len(s)>=3 and all(0x20<=c<0x7f for c in s): return s.decode("latin1")
    return None

def taskname(tcb):
    for w in range(0,STRIDE,4):
        v=rd32(tcb+w)
        if v and (0x07ff0000<=v<0x08400000 or 0x08800000<=v<0x08a00000):
            s=rdstr(v)
            if s: return s
    return "?"

gdb.execute("set pagination off"); gdb.execute("set confirm off")
gdb.execute("set target-async off")
gdb.execute("target remote :1234")

cur=rd32(CURTCB); curtn=rd32((cur or 0)+32)
P("=== ATTACH pc=0x%08x cpsr=0x%08x  current-task=0x%x tcb=0x%08x ===" % (
    reg("pc"), reg("cpsr"), curtn if curtn is not None else 0, cur or 0))

# ---- 1. discover ALL wait-queue heads: scan for TCB pointers in kernel data + IRAM
P("\n--- WAIT-QUEUE HEADS (any kernel word == TCB ptr) ---")
edges={}   # addr -> task id
def scan(lo,hi,label):
    b=rdblk(lo,hi-lo)
    if not b:
        P("  (scan fail %s)"%label); return
    for off in range(0,len(b)-3,4):
        v=int.from_bytes(b[off:off+4],"little")
        if is_tcb_ptr(v):
            a=lo+off
            # skip the TCB array itself (internal links reported separately)
            edges[a]=tid(v)
scan(0x08970000,0x08990000,"kdata")
scan(0x22000000,0x2200c000,"iram")
for a in sorted(edges):
    reg_lbl=""
    if MBOX<=a<MBOX+0x60*MBOX_STRIDE:
        idx=(a-MBOX)//MBOX_STRIDE; reg_lbl="MBOX[0x%x]+%d"%(idx,(a-MBOX)%MBOX_STRIDE)
    elif SEMA<=a<SEMA+0x60*4:
        idx=(a-SEMA)//4; reg_lbl="SEMA[0x%x]"%idx
    elif TCB_BASE<=a<TCB_END:
        reg_lbl="TCB[0x%x]+%d(link)"%(tid(a),(a-TCB_BASE)%STRIDE)
    P("  @0x%08x -> task 0x%02x   %s" % (a, edges[a], reg_lbl))

# ---- 2. mailbox table raw (index near 0x52) and sema table raw
P("\n--- MAILBOX TABLE 0x%08x (stride 32), non-empty rows 0x40..0x60 ---"%MBOX)
for i in range(0x40,0x60):
    base=MBOX+i*MBOX_STRIDE
    w=[rd32(base+j*4) for j in range(8)]
    if any(x not in (0,None,0xffffffff) for x in w):
        flags="".join(" *T%02x@+%d"%(tid(x),k*4) for k,x in enumerate(w) if is_tcb_ptr(x))
        P("  mbox[0x%02x] @0x%08x: %s%s"%(i,base," ".join("%08x"%(x or 0) for x in w),flags))
P("--- SEMA TABLE 0x%08x rows 0x00..0x30 ---"%SEMA)
s=""
for i in range(0,0x30):
    v=rd32(SEMA+i*4); s+="%02x:%08x "%(i,v if v is not None else 0)
    if i%4==3: P("  "+s); s=""

# ---- 3. per-task enumeration with backtrace
P("\n--- TASKS (state 0x40=WAITING) ---")
for i in range(0,NTASK):
    tcb=TCB_BASE+i*STRIDE; tn=rd32(tcb+32)
    if tn!=i: continue
    entry=rd32(tcb+12); savedSP=rd32(tcb+16); state=rd8(tcb+28)
    rpc=rd32((savedSP or 0)+56) if savedSP else 0
    nm=taskname(tcb)
    # who queues on this task? (reverse: this task waits on object X if a queue head==this tcb)
    waits_on=[a for a,t in edges.items() if t==i]
    wstr=" waitq@"+",".join("0x%08x"%a for a in waits_on) if waits_on else ""
    flag=""
    if i==curtn: flag=" <<CURRENT"
    if i==0x25: flag+=" <<AUDIO-ENGINE"
    if i==0x20: flag+=" <<STREAMCACHE"
    P("t%02x '%s' st=0x%02x entry=0x%08x savedSP=0x%08x resumePC=0x%08x%s%s"%(
        i,nm,state or 0,entry or 0,savedSP or 0,rpc or 0,wstr,flag))
    # backtrace: code addrs on stack
    base=(savedSP or 0)+64; chain=[]
    for j in range(0,600,4):
        w=rd32(base+j)
        if code(w): chain.append(w)
        if len(chain)>=12: break
    if chain: P("    bt: "+" ".join("%08x"%c for c in chain))

gdb.execute("detach"); gdb.execute("quit")
