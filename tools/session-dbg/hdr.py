import gdb,sys
def reg(n): return int(gdb.parse_and_eval("$"+n))&0xffffffff
def P(*a): print(*a);sys.stdout.flush()
gdb.execute("set pagination off");gdb.execute("set confirm off");gdb.execute("set target-async off")
gdb.execute("target remote :1234")
P("pc=0x%08x lr=0x%08x sp=0x%08x cpsr=0x%08x"%(reg("pc"),reg("lr"),reg("sp"),reg("cpsr")))
cur=int.from_bytes(gdb.selected_inferior().read_memory(0x2200ac64,4).tobytes(),"little")
tn=int.from_bytes(gdb.selected_inferior().read_memory(cur+32,4).tobytes(),"little")
P("current TCB=0x%08x tasknum=0x%x"%(cur,tn))
pcs=[]
for k in range(20):
    gdb.execute("stepi",to_string=True); pcs.append(reg("pc"))
P("stepi: "+" ".join("%08x"%p for p in pcs))
lo,hi=min(pcs),max(pcs)
P("span 0x%08x..0x%08x %s"%(lo,hi,"TIGHT-SPIN" if hi-lo<0x40 else "progressing"))
gdb.execute("detach");gdb.execute("quit")
