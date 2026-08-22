import gdb
gdb.execute("set pagination off");gdb.execute("set confirm off")
gdb.execute("target remote :1234")
inf=gdb.selected_inferior()
def rd32(a):
    try: return int.from_bytes(inf.read_memory(a & 0xffffffff,4).tobytes(),"little")
    except: return None
def code(v):
    return v is not None and (0x08000000<=v<0x08a00000 or 0x22000000<=v<0x2200c000)
def heap(v):
    return v is not None and (0x09000000<=v<0x0c000000 or 0x0b000000<=v<0x0c000000)
TCBb=0x0897ff70
def TCB(i): return TCBb+i*52

# ---- unwind: dump stack, flag return addresses (word at v-4 is a bl/blx) ----
def unwind(name,tid):
    tcb=TCB(tid); tn=rd32(tcb+32)
    if tn!=tid:
        print("%s NOT PRESENT (tasknum=%s)"%(name,tn)); return
    savedSP=rd32(tcb+16); state=rd32(tcb+28)&0xff; rpc=rd32(savedSP+56)
    af=rd32(savedSP+4); op=rd32(af) if af else None
    print("=== %s t%02x state=%02x savedSP=%08x resumePC=%08x argframe=%08x op=%s ==="%(name,tid,state,savedSP,rpc,af or 0,op))
    if af:
        aw=[rd32(af+4*k) for k in range(10)]
        print("   argframe:"," ".join("%08x"%(x or 0) for x in aw))
    print("   --- stack call-chain (return addrs: instr at addr-4 is bl/blx) ---")
    for k in range(0,140):
        a=savedSP+4*k; v=rd32(a)
        if code(v):
            pi=rd32(v-4)
            isbl = pi is not None and ((pi & 0x0f000000)==0x0b000000 or (pi & 0x0ff000f0)==0x01200030 or (pi & 0xfe000000)==0xfa000000)
            tag="  <== RET (bl@-4)" if isbl else ""
            if isbl:
                print("     [sp+%3d]=%08x%s"%(4*k,v,tag))
    # also list heap pointers on stack (decoder objects)
    hp=set()
    for k in range(0,80):
        v=rd32(savedSP+4*k)
        if heap(v): hp.add(v)
    print("   heap ptrs on stack:"," ".join("%08x"%x for x in sorted(hp)[:12]))
    return savedSP

for nm,t in [("t2a-DECODER",0x2a),("t20-STREAMCACHE",0x20),("t25-ENGINE",0x25)]:
    unwind(nm,t); print()

# ---- mailbox table 0x0897783c stride 32 : dump mbox 9, 0x52, 0x4e ----
MB=0x0897783c
print("=== mailbox objects (table 0x0897783c stride 32) ===")
for mb in [0x09,0x4e,0x52]:
    o=MB+mb*32
    print("mbox 0x%02x @%08x:"%(mb,o)," ".join("%08x"%(rd32(o+4*j) or 0) for j in range(8)))
# scan all mailboxes for non-empty queues (head/tail differ or count!=0)
print("=== non-trivial mailboxes ===")
for mb in range(0,0x60):
    o=MB+mb*32
    w=[rd32(o+4*j) for j in range(8)]
    if any(x not in (0,0xffffffff,None) for x in w):
        print("  mbox 0x%02x:"%mb," ".join("%08x"%(x or 0) for x in w))

# ---- exchange/object table 0x08977440 stride 20 ----
OBJ=0x08977440
print("=== object/exchange table 0x08977440 stride 20 (idx 0..12) ===")
for idx in range(0,13):
    o=OBJ+idx*20
    print("  obj[%2d]@%08x:"%(idx,o)," ".join("%08x"%(rd32(o+4*j) or 0) for j in range(5)))

# ---- sema/tag table 0x0897fb1c ----
print("=== sema/tag table 0x0897fb1c non-empty (idx 0..0x30) ===")
for i in range(0,0x30):
    v=rd32(0x0897fb1c+i*4)
    if v not in (0,0xffffffff,None): print("  sema[%d/0x%x]=%08x"%(i,i,v))

gdb.execute("detach");gdb.execute("quit")
