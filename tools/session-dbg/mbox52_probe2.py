import gdb
gdb.execute('set pagination off');gdb.execute('set confirm off')
gdb.execute('target remote :1234')
inf=gdb.selected_inferior()
def rd32(a):
    try: return int.from_bytes(inf.read_memory(a&0xffffffff,4).tobytes(),'little')
    except: return None
def code(v): return v is not None and (0x08000000<=v<0x08a00000 or 0x22000000<=v<0x2200c000)
def dump(name,base,n):
    print('== %s @%08x:'%(name,base))
    for r in range(0,n*4,16):
        print('   +%3d:'%r,' '.join('%08x'%(rd32(base+r+4*j)or 0) for j in range(4)))
MB=0x0897783c; OBJ=0x220106d0
dump('OBJ_220106d0',OBJ,20)
dump('ENGOBJ_220106dc',0x220106dc,20)
dump('MCB_mbox52_0897827c',MB+0x52*32,8)
# obj+4 waiter queue: walk linked list
q=rd32(OBJ+4)
print('== doorbell queue head [OBJ+4]=%08x'%(q or 0))
seen=[]
n=q
for _ in range(6):
    if not n or n in seen: break
    seen.append(n)
    print('   node %08x: [+0]=%08x [+4]=%08x [+8]=%08x [+12]=%08x'%(n,rd32(n)or 0,rd32(n+4)or 0,rd32(n+8)or 0,rd32(n+12)or 0))
    n=rd32(n)
TCBb=0x0897ff70
for i,nm in [(0x25,'motor'),(0x2a,'decoder'),(0x20,'streamcache')]:
    tcb=TCBb+i*52; tn=rd32(tcb+32); st=rd32(tcb+28); sSP=rd32(tcb+16); entry=rd32(tcb+12)
    rpc=rd32(sSP+56) if sSP else None
    print('== t%02x %s tasknum=%s st=%08x entry=%08x sSP=%08x tcb=%08x resumePC=%08x'%(i,nm,hex(tn) if tn is not None else '?',st or 0,entry or 0,sSP or 0,tcb,rpc or 0))
    if sSP:
        rets=[]
        for k in range(0,240):
            v=rd32(sSP+4*k)
            if code(v):
                pi=rd32(v-4)
                if pi is not None and ((pi&0x0f000000)==0x0b000000 or (pi&0xfe000000)==0xfa000000):
                    rets.append((4*k,v))
        print('   RET:',' '.join('[+%d]%08x'%(o,v) for o,v in rets[:14]))
gdb.execute('detach');gdb.execute('quit')
