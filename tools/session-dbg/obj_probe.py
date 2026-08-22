import gdb
gdb.execute("set pagination off");gdb.execute("set confirm off")
gdb.execute("target remote :1234")
inf=gdb.selected_inferior()
def rd32(a):
    try: return int.from_bytes(inf.read_memory(a,4).tobytes(),"little")
    except: return None
TCBb=0x0897ff70
def TCB(i): return TCBb+i*52
targets={0x20:"t20",0x2a:"t2a",0x25:"t25",0x22:"t22",0x24:"t24"}
tcbmap={TCB(i):n for i,n in targets.items()}
OBJ=0x8977440
print("=== object table 0x8977440 stride20: queues containing targets ===")
for idx in range(0,90):
    o=OBJ+idx*20
    head=rd32(o)
    if head is None: continue
    node=head; seen=set()
    for _ in range(16):
        if not node or node in seen: break
        seen.add(node)
        if node in tcbmap:
            print("obj[%d]@%08x head=%08x queues %s"%(idx,o,head,tcbmap[node]))
        node=rd32(node)
print("=== raw object headers idx 0..16 (5 words) ===")
for idx in range(0,16):
    o=OBJ+idx*20
    print("obj[%2d]@%08x:"%(idx,o)," ".join("%08x"%(rd32(o+j*4) or 0) for j in range(5)))
print("=== which words anywhere in 0x8977440..0x8977900 == TCB(t20/t2a/t25) ===")
for a in range(0x8977440,0x8977a00,4):
    v=rd32(a)
    if v in tcbmap: print("  @%08x = %s"%(a,tcbmap[v]))
print("=== 0x08979b00..0x08979b80 (the t20 wait head wgraph found @0x08979b3c) ===")
for a in range(0x08979b00,0x08979b80,4):
    print("  @%08x = %08x"%(a,rd32(a) or 0))
print("=== sema table 0x0897fb1c non-empty ===")
for i in range(0,0x30):
    v=rd32(0x0897fb1c+i*4)
    if v not in (0xffffffff,0,None): print("  sema[%d]=%08x"%(i,v))
gdb.execute("detach");gdb.execute("quit")
