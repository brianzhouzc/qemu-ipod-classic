import gdb
gdb.execute('set pagination off');gdb.execute('set confirm off')
gdb.execute('target remote :1234')
inf=gdb.selected_inferior()
def rd32(a):
    try: return int.from_bytes(inf.read_memory(a&0xffffffff,4).tobytes(),'little')
    except: return None
def code(v): return v is not None and (0x08000000<=v<0x08a00000 or 0x22000000<=v<0x2200c000)
RPC={0x2200433c:'op1-WAITsema',0x22004028:'op14-RECVmbox',0x22004170:'op4-RECVexch',0x22004394:'op1b-WAITsema',0x220040d8:'op5-SENDexch'}
TCBb=0x0897ff70; DEC=0x0ba9a8a4; MB=0x0897783c; EX=0x0897fb1c
print('=== DECODER OBJ 0x%08x ==='%DEC)
vt=rd32(DEC); ctx=rd32(DEC+0x34)
print('  vtable=%08x  ctx[+0x34]=%08x  inflag[+0x50]=%02x'%(vt or 0, ctx or 0,(rd32(DEC+0x50)or 0)&0xff))
if ctx:
    print('  --- ctx dump 0x%08x ---'%ctx)
    for r in range(0,0x94,16):
        print('   +%3d:'%r,' '.join('%08x'%(rd32(ctx+r+4*j)or 0) for j in range(4)))
    codec=rd32(ctx+52)
    print('  codec-inst[ctx+52]=%08x'%(codec or 0))
    if codec:
        cvt=rd32(codec)
        print('    codec vtable=%08x  decodeFn[vt+0x68]=%08x [vt+0x14]=%08x'%(cvt or 0,rd32((cvt or 0)+0x68)or 0,rd32((cvt or 0)+0x14)or 0))
        for r in range(0,0x60,16):
            print('     +%3d:'%r,' '.join('%08x'%(rd32(codec+r+4*j)or 0) for j in range(4)))
def dump_task(i,name):
    tcb=TCBb+i*52; tn=rd32(tcb+32)
    if tn!=i:
        print('== t%02x (%s): absent (tn=%s)'%(i,name,hex(tn) if tn else '?')); return
    state=rd32(tcb+28)&0xff; sSP=rd32(tcb+16)
    rpc=rd32(sSP+56) if sSP else None
    print('== t%02x (%s) st=%02x savedSP=%08x resumePC=%08x [%s]'%(i,name,state,sSP or 0,rpc or 0,RPC.get(rpc,'?')))
for i,nm in [(0x2a,'AAC-decoder'),(0x25,'audio-motor'),(0x20,'StreamCache')]:
    dump_task(i,nm)
for m in (9,0x52,0x4e):
    b=MB+m*32
    print('== MBOX 0x%02x @%08x head=%08x wait=%04x seq=%d :'%(m,b,rd32(b)or 0,(rd32(b+4)or 0)&0xffff,rd32(b+24)or 0),' '.join('%08x'%(rd32(b+4*j)or 0) for j in range(8)))
    head=rd32(b)
    if head and 0x08000000<=head<0x0c000000:
        print('     msg@%08x:'%head,' '.join('%08x'%(rd32(head+4*j)or 0) for j in range(8)))
eb=EX+6*32
print('== EXCH 6 @%08x:'%eb,' '.join('%08x'%(rd32(eb+4*j)or 0) for j in range(8)))
print('== mixer src 0x08889098:',' '.join('%08x'%(rd32(0x08889098+4*j)or 0) for j in range(8)))
print('== descr 0x0bb12d54:',' '.join('%08x'%(rd32(0x0bb12d54+4*j)or 0) for j in range(8)))
gdb.execute('detach');gdb.execute('quit')
