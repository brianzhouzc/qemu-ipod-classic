import gdb
gdb.execute("set pagination off"); gdb.execute("set confirm off")
gdb.execute("target remote :1234")
inf=gdb.selected_inferior()
def rd32(a):
    try: return int.from_bytes(inf.read_memory(a & 0xffffffff,4).tobytes(),"little")
    except: return None
def heap(v): return v is not None and 0x09000000<=v<0x0c000000
VT=0x088d1948
TCBb=0x0897ff70
def TCB(i): return TCBb+i*52
print("PC=%x"%int(gdb.parse_and_eval("$pc")))
# --- find decoder_obj: scan decoder task (0x2a) stack for heap ptr p with [p]==VT ---
def scan_task(tid,name):
    tcb=TCB(tid); tn=rd32(tcb+32)
    ssp=rd32(tcb+16); st=rd32(tcb+28)
    print("%s t%02x tasknum=%s state=%s savedSP=%08x"%(name,tid,tn,hex(st or 0),ssp or 0))
    found=[]
    if ssp:
        for k in range(0,200):
            v=rd32(ssp+4*k)
            if heap(v) and rd32(v)==VT: found.append(v)
    return found
cand=set()
for tid,nm in [(0x2a,"DECODER"),(0x20,"STREAMCACHE"),(0x25,"ENGINE")]:
    for f in scan_task(tid,nm): cand.add(f)
# also brute scan a heap window for objects with vtable VT (bounded)
print("--- brute heap scan for [p]==%08x ---"%VT)
import struct
for base in range(0x09000000,0x0b800000,0x100000):
    try: blob=inf.read_memory(base,0x100000).tobytes()
    except: continue
    off=0
    while True:
        i=blob.find(struct.pack("<I",VT),off)
        if i<0: break
        cand.add(base+i)  # address holding VT  == candidate obj+0
        off=i+4
        if len(cand)>40: break
    if len(cand)>40: break
print("candidates (obj where [obj+0]==VT): %s"%(" ".join("%08x"%c for c in sorted(cand))))
for o in sorted(cand):
    ctx=rd32(o+0x34); gate=rd32(ctx+0x60) if ctx else None
    port=o+0x54
    print("  obj=%08x [obj+0]=%08x ctx[+34]=%08x gate[[+34]+60]=%s decport(obj+54)=%08x [obj+54]=%08x"%(
        o, rd32(o) or 0, ctx or 0, hex(gate) if gate is not None else "?", port, rd32(o+0x54) or 0))
# --- mailbox structs ---
print("--- mbox9 @0x0897795c ---")
for k in range(0,8): print("   [+%02x]=%08x"%(k*4, rd32(0x0897795c+k*4) or 0))
print("--- mbox0x52 @0x0897827c ---")
for k in range(0,8): print("   [+%02x]=%08x"%(k*4, rd32(0x0897827c+k*4) or 0))
# --- engine port 0x220106d0 ---
print("--- engine port 0x220106d0 (first 0x40) ---")
for k in range(0,16): print("   [+%02x]=%08x"%(k*4, rd32(0x220106d0+k*4) or 0))
gdb.execute("detach"); gdb.execute("quit")
