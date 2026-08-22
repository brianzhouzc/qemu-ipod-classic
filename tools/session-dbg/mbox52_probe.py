import gdb
gdb.execute('set pagination off');gdb.execute('set confirm off')
gdb.execute('target remote :1234')
inf=gdb.selected_inferior()
def rd32(a):
    try: return int.from_bytes(inf.read_memory(a&0xffffffff,4).tobytes(),'little')
    except: return None
def dump(name,base,n=16):
    print('== %s @%08x:'%(name,base))
    for r in range(0,n*4,16):
        print('   +%3d:'%r,' '.join('%08x'%(rd32(base+r+4*j)or 0) for j in range(4)))
MB=0x0897783c
OBJ=0x220106d0
dump('OBJ_220106d0',OBJ,16)
dump('MCB_mbox52',MB+0x52*32,8)
dump('MCB_mbox9',MB+9*32,8)
TCBb=0x0897ff70
for i,nm in [(0x25,'motor'),(0x2a,'decoder'),(0x20,'streamcache')]:
    tcb=TCBb+i*52; tn=rd32(tcb+32); st=rd32(tcb+28); sSP=rd32(tcb+16); entry=rd32(tcb+12)
    rpc=rd32(sSP+56) if sSP else None
    print('== t%02x %s tasknum=%s st=%08x entry=%08x sSP=%08x resumePC=%08x'%(i,nm,hex(tn) if tn is not None else '?',st or 0,entry or 0,sSP or 0,rpc or 0))
gdb.execute('detach');gdb.execute('quit')
