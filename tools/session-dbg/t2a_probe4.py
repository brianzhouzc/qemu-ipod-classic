import gdb
gdb.execute("set pagination off");gdb.execute("set confirm off")
gdb.execute("target remote :1234")
inf=gdb.selected_inferior()
def rd32(a):
    try: return int.from_bytes(inf.read_memory(a & 0xffffffff,4).tobytes(),"little")
    except: return None
def rdb(a,n):
    try: return inf.read_memory(a & 0xffffffff,n).tobytes()
    except: return None
def heapp(v): return v is not None and (0x09000000<=v<0x0c000000)
TCBb=0x0897ff70
def TCB(i): return TCBb+i*52
ct=rd32(0x22010730+4)  # +0x58 type dword; low byte is type
print("CODEC type byte=0x%x (0xff=teardown)"%((ct or 0)&0xff))
# t2a wait state
tcb=TCB(0x2a); savedSP=rd32(tcb+16); state=rd32(tcb+28)&0xff; af=rd32(savedSP+4)
op=rd32(af) if af else None
print("t2a state=0x%02x op=%s (14=RECVmbox=starved-pull)"%(state,op))
# find decoder obj
cands=[]
for k in range(0,160):
    v=rd32(savedSP+4*k)
    if heapp(v) and v not in cands: cands.append(v)
dec=None
for C in cands:
    o80=rdb(C+80,1); o84=rd32(C+84); o52=rd32(C+52)
    if o80 and o80[0] in (0,1) and o84 and 0x08000000<=o84<0x0c000000 and o52 and 0x08000000<=o52<0x0c000000:
        dec=C; break
print("decoder obj=%s"%(("0x%08x"%dec) if dec else "NOTFOUND"))
if dec:
    o52=rd32(dec+52)
    inrdy=rd32(o52+96)
    print("stream[+52]=0x%08x  INPUT-READY-count [[+52]+96]=%s"%(o52,str(inrdy)))
    print("  stream 0x%08x words:"%o52," ".join("%08x"%(rd32(o52+4*j) or 0) for j in range(28)))
    # walk stream ptrs for an input buffer holding compressed AAC bytes
    print("  --- scanning stream sub-pointers for AAC-looking data (ADTS 0xFFFx / non-zero) ---")
    for j in range(0,28):
        p=rd32(o52+4*j)
        if heapp(p):
            b=rdb(p,16)
            if b:
                nz=sum(1 for x in b if x!=0)
                print("   [+%3d]->0x%08x : %s  (nonzero %d/16)"%(4*j,p," ".join("%02x"%x for x in b),nz))
gdb.execute("detach");gdb.execute("quit")
