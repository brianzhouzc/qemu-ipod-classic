import gdb
gdb.execute("set pagination off");gdb.execute("set confirm off")
gdb.execute("target remote :1234")
inf=gdb.selected_inferior()
def rd32(a):
    try: return int.from_bytes(inf.read_memory(a & 0xffffffff,4).tobytes(),"little")
    except: return None
TCBb=0x0897ff70
def TCB(i): return TCBb+i*52
def isptr(v): return v is not None and (0x08000000<=v<0x08a00000 or 0x0b000000<=v<0x0c000000 or 0x22000000<=v<0x2200c000 or 0x09000000<=v<0x0a000000)
for name,tid,opc in [("t20",0x20,4),("t2a",0x2a,14),("t25",0x25,14),("t22",0x22,None),("t24",0x24,None)]:
    tcb=TCB(tid); savedSP=rd32(tcb+16); state=rd32(tcb+28); rpc=rd32(savedSP+56)
    print("%s tcb=%08x savedSP=%08x state=%02x resumePC=%08x"%(name,tcb,savedSP,state&0xff,rpc))
    # dump 40 words of the saved frame
    words=[rd32(savedSP+4*i) for i in range(40)]
    # find arg-frame: a pointer P in the frame where [P]==opcode(small) and args plausible
    for i,w in enumerate(words):
        if isptr(w):
            op=rd32(w)
            if op is not None and op<0x40:
                idx=rd32(w+12); a1=rd32(w+16)
                print("   [savedSP+%d]=%08x  ->[0]=%d(opcode) [+12]=idx=0x%x [+16]=0x%x"%(4*i,w,op,(idx or 0)&0xffffffff,(a1 or 0)&0xffffffff))
    print("   frame:", " ".join("%08x"%(w or 0) for w in words[:20]))
gdb.execute("detach");gdb.execute("quit")
