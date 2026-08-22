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
def bt(tcb,tn,limit=40):
    savedSP=rd32(tcb+16); state=rd8(tcb+28)
    P("  task 0x%02x tcb=0x%08x state=0x%02x savedSP=0x%08x" % (tn,tcb,state or 0,savedSP or 0))
    rpc=rd32((savedSP or 0)+56); P("    resumePC(saved lr)=0x%08x" % (rpc or 0))
    # walk stack for return addresses
    base=(savedSP or 0)+64
    chain=[]
    for j in range(0,limit*4,4):
        w=rd32(base+j)
        if code(w): chain.append((64+j,w))
    for off,w in chain[:24]:
        P("      [sp+0x%03x]=0x%08x" % (off,w))
gdb.execute("set pagination off");gdb.execute("set confirm off");gdb.execute("set target-async off")
gdb.execute("target remote :1234")
P("pc=0x%08x cpsr=0x%08x" % (reg("pc"),reg("cpsr")))
# find task 0x20 and the non-0x40 tasks
targets=[]
for i in range(0,64):
    tcb=TCB_BASE+i*STRIDE; tn=rd32(tcb+32); state=rd8(tcb+28)
    if tn!=i: continue
    if i==0x20 or (state not in (0x40,0x00) and state is not None):
        targets.append((tcb,i,state))
P("sema[6]=0x%08x (waiter task)" % (rd32(SEMA+6*4) or 0))
for tcb,i,state in targets:
    P("==== BT task 0x%02x (state 0x%02x) ====" % (i,state or 0))
    bt(tcb,i)
gdb.execute("detach");gdb.execute("quit")
