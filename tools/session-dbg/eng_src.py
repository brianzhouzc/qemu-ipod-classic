import gdb
gdb.execute("set pagination off"); gdb.execute("set confirm off")
gdb.execute("target remote :1234")
inf=gdb.selected_inferior()
def rd32(a):
    try: return int.from_bytes(inf.read_memory(a&0xffffffff,4).tobytes(),"little")
    except: return None
def rd8(a):
    try: return inf.read_memory(a&0xffffffff,1).tobytes()[0]
    except: return None
def dump(a,n=8): return " ".join("%08x"%(rd32(a+4*i) or 0) for i in range(n))
def pcmlike(a):
    try: b=inf.read_memory(a&0xffffffff,128).tobytes()
    except: return "unreadable"
    import struct; v=struct.unpack("<64h",b)
    return "nz=%d/64 max=%d filler=%d"%(sum(1 for x in v if x!=0),max(abs(x) for x in v),b.count(b"\xa3\x5d"))
h=lambda v:("%08x"%v) if v is not None else "None"
# render-state pointer literal at 0x22003034
p0=rd32(0x22003034); print("*0x22003034 (render-state r5) = %s"%h(p0))
p0b=rd32(0x22003030); print("*0x22003030 (r0 arg) = %s"%h(p0b))
if p0:
    print("  r5[0..0x20]:", dump(p0,8))
    src_obj=rd32(p0+4); print("  src_obj=[r5+4]=%s"%h(src_obj))
    if src_obj:
        print("    src_obj[0..0x20]:", dump(src_obj,8))
        inner=rd32(src_obj); print("    inner=[src_obj]=%s"%h(inner))
        if inner:
            print("      inner[0..0x40]:", dump(inner,16))
            print("      [inner+5]ready=%s"%h(rd8(inner+5)))
            vt=rd32(inner); print("      inner vtable=%s [vt+28]=%s(poll method)"%(h(vt),h(rd32((vt or 0)+28))))
            # look for a PCM buffer pointer in inner
            for off in range(0,0x40,4):
                q=rd32(inner+off)
                if q and 0x08000000<=q<0x0c000000:
                    print("        [inner+0x%02x]=%s -> %s | %s"%(off,h(q),dump(q,4),pcmlike(q)))
gdb.execute("detach"); gdb.execute("quit")
