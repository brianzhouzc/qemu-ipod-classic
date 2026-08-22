# Catch StartPlayback, then log every RTXC block (task sleeps on empty sema).
import gdb, time, sys

CURTCB    = 0x2200ac64
TCB_BASE  = 0x0897ff70
TCB_STRIDE= 52
SEMA      = 0x0897fb1c
STARTPLAY = 0x08150d84
BLOCKPT   = 0x2200079c

def rd32(a):
    try: return int.from_bytes(gdb.selected_inferior().read_memory(a,4).tobytes(),"little")
    except Exception: return None
def rd8(a):
    try: return gdb.selected_inferior().read_memory(a,1).tobytes()[0]
    except Exception: return None
def reg(n):
    return int(gdb.parse_and_eval("$"+n)) & 0xffffffff
def P(*a):
    print(*a); sys.stdout.flush()

def full_enum(tag):
    P("\n===== FULL TCB ENUM (%s) =====" % tag)
    cur = rd32(CURTCB)
    P("current TCB = 0x%08x  tasknum=0x%x" % (cur or 0, rd32((cur or 0)+32) or 0))
    P("id  state entry      savedSP    resumePC   waitObj(sem)")
    for i in range(0,64):
        tcb=TCB_BASE+i*TCB_STRIDE
        entry=rd32(tcb+12); savedSP=rd32(tcb+16); state=rd8(tcb+28); tasknum=rd32(tcb+32)
        if tasknum!=i: continue
        rpc=rd32((savedSP or 0)+56) if savedSP else 0
        rb=rd32((savedSP or 0)+4) if savedSP else 0
        wo=rd32((rb or 0)+8) if rb else None
        P("%02x  0x%02x  0x%08x 0x%08x 0x%08x %s%s" % (
            i, state or 0, entry or 0, savedSP or 0, rpc or 0,
            ("sem=0x%x"%wo) if wo is not None else "-",
            "   <== WAITING" if (state==0x40) else ""))
    P("--- sema array (id:val  FFFFFFFF=empty 0=done else=waiter-node) ---")
    s=""
    for i in range(0,48):
        v=rd32(SEMA+i*4); s+="%02x:%08x " % (i,v if v is not None else 0)
        if i%4==3: P("  "+s); s=""

def main():
    gdb.execute("set pagination off"); gdb.execute("set confirm off")
    gdb.execute("target remote :1234")
    gdb.execute("break *0x%08x" % STARTPLAY)
    P("waiting for StartPlayback...")
    gdb.execute("continue")
    P("=== StartPlayback hit pc=0x%08x ===" % reg("pc"))
    gdb.execute("delete")
    gdb.execute("break *0x%08x" % BLOCKPT)
    seen20=0
    P("logging block events (task, sem, resumePC):")
    P("  n  task  sem  resumePC   reqblk")
    for k in range(60):
        gdb.execute("continue")
        if (reg("pc") & 0xffffffff)!=BLOCKPT:
            P("  stopped elsewhere pc=0x%08x" % reg("pc")); break
        cur=rd32(CURTCB); tn=rd32((cur or 0)+32); sem=reg("fp"); r4=reg("r4")
        savedSP=rd32((cur or 0)+16); rpc=rd32((savedSP or 0)+56) if savedSP else 0
        tag = "  <== TASK 0x20 (decode)" if tn==0x20 else ""
        P("  %2d  0x%02x  0x%02x 0x%08x 0x%08x%s" % (k, tn or 0, sem or 0, rpc or 0, r4 or 0, tag))
        if tn==0x20:
            seen20+=1
            # dump stack around savedSP for a backtrace of the decode chain
            P("     decode savedSP=0x%08x frame:" % (savedSP or 0))
            for off in range(0,80,16):
                w=[rd32((savedSP or 0)+off+j*4) for j in range(4)]
                P("       +%02x: %08x %08x %08x %08x" % (off,*[x or 0 for x in w]))
            if seen20>=1:
                # walk further up the task stack for return addresses (decode fn)
                P("     stack scan for code addrs above frame:")
                base=(savedSP or 0)+64
                cnt=0
                for j in range(0,256,4):
                    w=rd32(base+j)
                    if w and ((0x08000000<=w<0x08400000)):
                        P("       [sp+0x%03x]=0x%08x" % (64+j, w)); cnt+=1
                        if cnt>=16: break
                break
    full_enum("after task 0x20 blocked" if seen20 else "settled/timeout")
    gdb.execute("detach"); gdb.execute("quit")

main()
