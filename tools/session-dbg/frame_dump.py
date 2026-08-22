import gdb
gdb.execute("set pagination off");gdb.execute("set confirm off")
gdb.execute("target remote :1234")
inf=gdb.selected_inferior()
def rd32(a):
    try: return int.from_bytes(inf.read_memory(a&0xffffffff,4).tobytes(),"little")
    except: return None
TCBb=0x0897ff70
RPC={0x2200433c:"op1-WAITsema",0x22004028:"op14-RECVmbox",0x22004170:"op4-RECVexch",0x22004394:"op1b-WAITsema",0x220040d8:"op5-SENDexch",0x22004360:"op34",0x22004224:"op25",0x22003d14:"op?"}
for i in [0x16,0x0e,0x20,0x2a,0x25,0x09,0x13]:
    tcb=TCBb+i*52; tn=rd32(tcb+32)
    if tn!=i: 
        print("t%02x not present"%i); continue
    state=rd32(tcb+28)&0xff; savedSP=rd32(tcb+16); rpc=rd32(savedSP+56)
    print("=== t%02x st=%02x savedSP=%08x rpc=%08x [%s] ==="%(i,state,savedSP,rpc,RPC.get(rpc,"?")))
    fr=[rd32(savedSP+4*k) for k in range(20)]
    print("  savedSP frame:", " ".join("%08x"%(x or 0) for x in fr))
    af=fr[1]
    if af and 0x08000000<=af<0x0c000000 or (af and 0x22000000<=af<0x2200c000):
        afw=[rd32(af+4*k) for k in range(12)]
        print("  argframe@%08x:"%af, " ".join("%08x"%(x or 0) for x in afw))
gdb.execute("detach");gdb.execute("quit")
