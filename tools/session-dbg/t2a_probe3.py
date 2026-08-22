import gdb
gdb.execute("set pagination off");gdb.execute("set confirm off")
gdb.execute("target remote :1234")
inf=gdb.selected_inferior()
def rd32(a):
    try: return int.from_bytes(inf.read_memory(a & 0xffffffff,4).tobytes(),"little")
    except: return None
def rd8(a):
    try: return inf.read_memory(a & 0xffffffff,1).tobytes()[0]
    except: return None
def heapp(v): return v is not None and (0x09000000<=v<0x0c000000)
TCBb=0x0897ff70
def TCB(i): return TCBb+i*52

# codec teardown check
ct=rd8(0x22010734)
print("CODEC type[+58]=0x%x (0xff => POST-TEARDOWN; else pre-teardown/clean)"%(ct or 0))

# engine struct 0x220106d0 + its mailbox 0x52 (owner of render trigger)
print("=== engine struct 0x220106d0 (0x220106c0..0x22010700) ===")
for a in range(0x220106c0,0x22010700,16):
    print("  %08x:"%a," ".join("%08x"%(rd32(a+4*j) or 0) for j in range(4)))
# mbox 0x52 object
MB=0x0897783c
o=MB+0x52*32
print("=== mbox 0x52 @%08x:"%o," ".join("%08x"%(rd32(o+4*j) or 0) for j in range(8)),"===")
print("   ([2]=msgqueue head, [0]=waiter?, [6]=seq). If [2]!=0 a trigger is QUEUED but unconsumed.")
o9=MB+9*32
print("=== mbox 0x09 @%08x:"%o9," ".join("%08x"%(rd32(o9+4*j) or 0) for j in range(8)),"===")

# decoder object via t2a stack
tcb=TCB(0x2a); savedSP=rd32(tcb+16)
cands=[]
for k in range(0,160):
    v=rd32(savedSP+4*k)
    if heapp(v) and v not in cands: cands.append(v)
dec=None
for C in cands:
    o80=rd8(C+80); o84=rd32(C+84); o52=rd32(C+52)
    if (o80 in (0,1)) and o84 and 0x08000000<=o84<0x0c000000 and o52 and 0x08000000<=o52<0x0c000000:
        dec=C; break
print("=== DECODER obj @%s ==="%(("%08x"%dec) if dec else "NOT FOUND"))
if dec:
    o52=rd32(dec+52)
    print("  [+52 stream]=%08x [+80 initflag]=%d [+84 port]=%08x [+136 bufcnt]=%d"%(o52,rd8(dec+80),rd32(dec+84),rd32(dec+136)))
    g=rd32(o52+96) if o52 else None
    print("  DECODE-GATE [[+52]+96]=%s"%str(g))
    # buffer pool: follow the chain of descriptors around +96..+148
    print("  pool ptrs:", " ".join("%08x"%(rd32(dec+4*j) or 0) for j in range(24,40)))
    # walk each distinct heap ptr descriptor and read owner/state bytes
    seen=set()
    for j in range(0,40):
        p=rd32(dec+4*j)
        if heapp(p) and p not in seen and p!=dec:
            seen.add(p)
            print("   desc@%08x: [0..7]="%p," ".join("%08x"%(rd32(p+4*k) or 0) for k in range(8)),"  b[4,5,6,7]=",[rd8(p+4),rd8(p+5),rd8(p+6),rd8(p+7)])
            if len(seen)>=10: break

# who owns exch6 / obj1 (t20 waiting there) - the upstream credit
OBJ=0x08977440
print("=== obj1@%08x:"%(OBJ+1*20)," ".join("%08x"%(rd32(OBJ+1*20+4*j) or 0) for j in range(5)),"(t20 recv here) ===")
print("=== obj4@%08x:"%(OBJ+4*20)," ".join("%08x"%(rd32(OBJ+4*20+4*j) or 0) for j in range(5)),"(t0e recv here) ===")
gdb.execute("detach");gdb.execute("quit")
