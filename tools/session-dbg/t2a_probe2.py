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
def code(v): return v is not None and (0x08000000<=v<0x08a00000 or 0x22000000<=v<0x2200c000)
def heapp(v): return v is not None and (0x09000000<=v<0x0c000000)
TCBb=0x0897ff70
def TCB(i): return TCBb+i*52

# t2a saved stack: find r4 saved by 0x8123508 (push{r4,lr}) = the decoder object.
tcb=TCB(0x2a); savedSP=rd32(tcb+16)
print("t2a savedSP=%08x"%savedSP)
# gather all heap ptr candidates on the stack
cands=[]
for k in range(0,160):
    v=rd32(savedSP+4*k)
    if heapp(v) and v not in cands: cands.append(v)
print("heap candidates:", " ".join("%08x"%c for c in cands))

def dumpobj(tag,C):
    print("--- %s obj @%08x ---"%(tag,C))
    w=[rd32(C+4*j) for j in range(40)]
    for base in range(0,40,8):
        print("   +%3d:"%(base*4)," ".join("%08x"%(w[base+j] or 0) for j in range(8)))
    o52=rd32(C+52); o84=rd32(C+84); o80=rd8(C+80)
    print("   [+52]=%08x [+80]=%s [+84]=%08x [+34]=%08x [+38]=%08x [+56]=%08x [+76]=%08x [+132]=%08x [+136]=%08x"%(
        (o52 or 0),o80,(o84 or 0),(rd32(C+0x34) or 0),(rd32(C+0x38) or 0),(rd32(C+0x38) or 0),(rd32(C+76) or 0),(rd32(C+132) or 0),(rd32(C+136) or 0)))
    if o52 and (0x08000000<=o52<0x0c000000):
        g=rd32(o52+96)
        print("   [[+52]+96] (DECODE-GATE)=%s   ([+52]+80..100:"%str(g), " ".join("%08x"%(rd32(o52+80+4*j) or 0) for j in range(6)),")")
    if o84 and (0x08000000<=o84<0x0c000000):
        print("   decode-subobj[+84]@%08x:"%o84," ".join("%08x"%(rd32(o84+4*j) or 0) for j in range(12)))

# Identify decoder object: field [+84] is a code/heap ptr, [+80] in {0,1}
for C in cands:
    o80=rd8(C+80); o84=rd32(C+84); o52=rd32(C+52)
    isdec = (o80 in (0,1)) and o84 is not None and (0x08000000<=o84<0x0c000000) and o52 is not None and (0x08000000<=o52<0x0c000000)
    if isdec:
        dumpobj("CANDIDATE-DECODER",C)

# also: mbox9 pending message queue. head fields from prior probe:
MB=0x0897783c
o=MB+9*32
mw=[rd32(o+4*j) for j in range(8)]
print("=== mbox9 @%08x:"%o," ".join("%08x"%(x or 0) for x in mw),"===")
for fld in (0,2):
    p=mw[fld]
    if p and 0x08000000<=p<0x0c000000:
        print("   msgnode[%d]@%08x:"%(fld,p)," ".join("%08x"%(rd32(p+4*j) or 0) for j in range(10)))

# engine t25 codec struct @0x220106d0/0x220106dc (type/gate)
print("=== codec state 0x220106c0..0x22010740 ===")
for a in range(0x220106c0,0x22010744,16):
    print("   %08x:"%a," ".join("%08x"%(rd32(a+4*j) or 0) for j in range(4)))
print("   type[+58]=0x22010734 ->",hex(rd8(0x22010734) or 0)," gate[+47]=0x22010723 ->",hex(rd8(0x22010723) or 0))

gdb.execute("detach");gdb.execute("quit")
