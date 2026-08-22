import gdb
gdb.execute("set pagination off");gdb.execute("set confirm off")
gdb.execute("target remote :1234")
inf=gdb.selected_inferior()
def rd32(a):
    try: return int.from_bytes(inf.read_memory(a&0xffffffff,4).tobytes(),"little")
    except: return None
TCBb=0x0897ff70
OPN={1:"WAIT-sema",2:"SIGNAL",3:"?3",4:"RECV-exch",5:"SEND-exch",14:"RECV-mbox",26:"?26"}
print("=== per-task wait decode (object index / tag / mailbox) ===")
for i in range(0,0x40):
    tcb=TCBb+i*52
    tn=rd32(tcb+32)
    if tn!=i: continue
    state=rd32(tcb+28)&0xff
    if state==0: continue
    savedSP=rd32(tcb+16); rpc=rd32(savedSP+56)
    af=rd32(savedSP+4)
    if af is None: continue
    op=rd32(af)
    w=[rd32(af+4*k) for k in range(9)]
    desc=""
    if op==4:   desc="obj=%d tag=%d"%((w[3] or 0)&0xffffffff,(w[2] or 0)&0xffffffff)
    elif op==14:desc="mbox=0x%x"%((w[2] or 0)&0xffffffff)
    elif op==1: desc="sema/tag=%d"%((w[2] or 0)&0xffffffff)
    elif op==2: desc="sigtag=%d"%((w[2] or 0)&0xffffffff)
    else:       desc="af="+",".join("%x"%(x or 0) for x in w[:5])
    print("t%02x st=%02x rpc=%08x op=%s(%s) %s"%(i,state,rpc,str(op),OPN.get(op,"?"),desc))
gdb.execute("detach");gdb.execute("quit")
