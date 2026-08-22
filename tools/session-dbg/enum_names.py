import gdb,sys
TCB_BASE=0x0897ff70; STRIDE=52; SEMA=0x0897fb1c
def rd32(a):
    try: return int.from_bytes(gdb.selected_inferior().read_memory(a,4).tobytes(),"little")
    except Exception: return None
def rdstr(a):
    try:
        b=gdb.selected_inferior().read_memory(a,24).tobytes()
        s=b.split(b"\x00")[0]
        if len(s)>=3 and all(0x20<=c<0x7f for c in s): return s.decode("latin1")
    except Exception: pass
    return None
def P(*a): print(*a);sys.stdout.flush()
gdb.execute("set pagination off");gdb.execute("set confirm off")
gdb.execute("target remote :1234")
P("=== task names (any TCB word pointing to a string) ===")
for i in range(0,0x40):
    tcb=TCB_BASE+i*STRIDE; tn=rd32(tcb+32)
    if tn!=i: continue
    entry=rd32(tcb+12); sem=None
    names=[]
    for w in range(0,52,4):
        v=rd32(tcb+w)
        if v and (0x07ff0000<=v<0x08400000 or 0x08800000<=v<0x08a00000):
            s=rdstr(v)
            if s: names.append("+%d->%s"%(w,s))
    P("t%02x entry=0x%08x  %s" % (i, entry or 0, "  ".join(names) if names else "(no name ptr in TCB)"))
gdb.execute("detach");gdb.execute("quit")
