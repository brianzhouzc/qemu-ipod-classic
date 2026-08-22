import gdb,sys
CURTCB=0x2200ac64; SEMA=0x0897fb1c
def rd32(a):
    try: return int.from_bytes(gdb.selected_inferior().read_memory(a,4).tobytes(),"little")
    except Exception: return None
def reg(n): return int(gdb.parse_and_eval("$"+n))&0xffffffff
def P(*a): print(*a);sys.stdout.flush()
def code(a): return a is not None and (0x08000000<=a<0x08400000 or 0x22000000<=a<0x2200c000)
def curtask(): 
    c=rd32(CURTCB); return rd32((c or 0)+32)
def bt():
    sp=reg("sp"); chain=[]
    for j in range(0,600,4):
        w=rd32(sp+j)
        if code(w): chain.append((j,w))
        if len(chain)>=14: break
    return chain
gdb.execute("set pagination off");gdb.execute("set confirm off")
gdb.execute("set mi-async off")
gdb.execute("target remote :1234")
gdb.execute("break *0x08150d84")   # StartPlayback
P("continue -> StartPlayback")
gdb.execute("continue")
P("stopped pc=0x%08x (want 08150d84)" % reg("pc"))
gdb.execute("delete")
gdb.execute("break *0x0808170c")   # audio engine
gdb.execute("continue")
P("stopped pc=0x%08x (want 0808170c)" % reg("pc"))
etask=curtask()
P("=== AUDIO ENGINE runs in task 0x%02x  lr=0x%08x sp=0x%08x ===" % (etask or 0, reg("lr"), reg("sp")))
P("caller chain (stack code addrs):")
for off,w in bt(): P("   [sp+0x%03x]=0x%08x" % (off,w))
# now follow this task until it blocks
gdb.execute("delete")
gdb.execute("break *0x2200079c")   # block point (task sleeps on empty sema)
P("following task 0x%02x to its block..." % (etask or 0))
found=False
for k in range(300):
    gdb.execute("continue")
    if (reg("pc")&0xffffffff)!=0x2200079c:
        P("  left block region pc=0x%08x" % reg("pc")); break
    tn=curtask()
    if tn==etask:
        sem=reg("fp"); c=rd32(CURTCB); ssp=rd32((c or 0)+16); rpc=rd32((ssp or 0)+56)
        P("  *** task 0x%02x BLOCKS on sem 0x%x  resumePC=0x%08x (hit %d) ***" % (tn,sem,rpc or 0,k))
        # backtrace of the blocking chain from this task stack
        base=(ssp or 0)+64; chain=[]
        for j in range(0,500,4):
            w=rd32(base+j)
            if code(w): chain.append(w)
            if len(chain)>=12: break
        P("  block bt: "+" ".join("%08x"%x for x in chain[:10]))
        found=True; break
if not found: P("  engine task did not reach block in 300 hits")
gdb.execute("detach");gdb.execute("quit")
