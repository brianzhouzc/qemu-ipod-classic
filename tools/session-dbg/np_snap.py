import gdb
gdb.execute("set pagination off"); gdb.execute("set confirm off")
gdb.execute("target remote :1234")
inf=gdb.selected_inferior()
def rd32(a):
    try: return int.from_bytes(inf.read_memory(a&0xffffffff,4).tobytes(),"little")
    except: return None
def dump(a,n=8):
    return " ".join("%08x"%(rd32(a+4*i) or 0) for i in range(n))
def pcmlike(a):
    # sample 64 int16 values; PCM = varied, small-ish, not 0x5da3 filler, not ptrs
    try: b=inf.read_memory(a&0xffffffff,128).tobytes()
    except: return "unreadable"
    import struct
    v=struct.unpack("<64h",b)
    nz=sum(1 for x in v if x!=0); filler=b.count(b"\xa3\x5d")
    mx=max(abs(x) for x in v)
    return "nz=%d/64 max=%d filler5da3=%d"%(nz,mx,filler)
h=lambda v:("%08x"%v) if v is not None else "None"
DEC=0x0ba9a8a4
ctx=rd32(DEC+0x34); print("ctx=%s"%h(ctx))
reader=rd32((ctx or 0)+0x48); print("reader=%s"%h(reader))
if reader:
    print("reader[0..0x40]:", dump(reader,16))
    for off in (4,8,0xc,0x10,0x14,0x18,0x1c,0x20,0x30,0x34,0x38,0x3c):
        p=rd32(reader+off)
        if p and 0x08000000<=p<0x0c000000:
            print("  [reader+0x%02x]=%s -> %s | %s"%(off,h(p),dump(p,4),pcmlike(p)))
    sub=rd32(reader+4)
    if sub:
        print("  sub=[reader+4]=%s  [sub+0x38]=%s [+0x3c]=%s [+0x40]=%s [+0x44]=%s"%(
            h(sub),h(rd32(sub+0x38)),h(rd32(sub+0x3c)),h(rd32(sub+0x40)),h(rd32(sub+0x44))))
        for off in (0,4,8,0xc,0x10,0x2b8):
            p=rd32(sub+off)
            if p and 0x08000000<=p<0x0c000000:
                print("    [sub+0x%03x]=%s -> %s | %s"%(off,h(p),dump(p,4),pcmlike(p)))
print("mixer-src 0x08889098: %s | %s"%(dump(0x08889098,4),pcmlike(0x08889098)))
gdb.execute("detach"); gdb.execute("quit")
