import gdb
gdb.execute('set pagination off');gdb.execute('set confirm off')
gdb.execute('target remote :1234')
inf=gdb.selected_inferior()
def rd32(a):
    try: return int.from_bytes(inf.read_memory(a&0xffffffff,4).tobytes(),'little')
    except: return None
def code(v): return v is not None and (0x08000000<=v<0x08a00000 or 0x22000000<=v<0x2200c000)
RPC={0x2200433c:'op1-WAITsema',0x22004028:'op14-RECVmbox',0x22004170:'op4-RECVexch',
     0x22004394:'op1b-WAITsema',0x220040d8:'op5-SENDexch',0x22004360:'op34',
     0x22004224:'op25',0x22004138:'op5-SENDexch(post)',0x220157a0:'fastsema'}
TCBb=0x0897ff70
DEC=0x0ba9a8a4
print('== DECODER OBJ %08x: vtable=%08x [+0x34]ctx=%08x [+0x50]inflag=%02x'%(
    DEC, rd32(DEC)or 0, rd32(DEC+0x34)or 0, (rd32(DEC+0x50)or 0)&0xff))
ctx=rd32(DEC+0x34)
if ctx: print('   ctx=%08x  gate[ctx+0x60]=%08x  [ctx+0x64]=%08x [ctx+0x68]=%08x'%(
    ctx, rd32(ctx+0x60)or 0, rd32(ctx+0x64)or 0, rd32(ctx+0x68)or 0))
def dump_task(i,name):
    tcb=TCBb+i*52; tn=rd32(tcb+32)
    if tn!=i:
        print('== t%02x (%s): NOT PRESENT (tasknum=%s)'%(i,name,hex(tn) if tn else '?')); return
    state=rd32(tcb+28)&0xff; sSP=rd32(tcb+16); entry=rd32(tcb+12)
    rpc=rd32(sSP+56) if sSP else None
    print('== t%02x (%s) st=%02x entry=%08x savedSP=%08x resumePC=%08x [%s]'%(
        i,name,state,entry or 0,sSP or 0,rpc or 0,RPC.get(rpc,'?')))
    if not sSP: return
    # saved ctx window
    print('   ctx[sSP+0..64]:')
    for r in range(0,80,16):
        print('    +%3d:'%r,' '.join('%08x'%(rd32(sSP+r+4*j)or 0) for j in range(4)))
    # return-address chain
    rets=[]
    for k in range(0,220):
        a=sSP+4*k;v=rd32(a)
        if code(v):
            pi=rd32(v-4)
            if pi is not None and ((pi&0x0f000000)==0x0b000000 or (pi&0xfe000000)==0xfa000000):
                rets.append((4*k,v))
    print('   RET chain:',' '.join('[+%d]%08x'%(o,v) for o,v in rets[:12]))
# The real AAC decoder is t2a; motor t25; StreamCache t20. Examine all three.
for i,nm in [(0x2a,'AAC-decoder'),(0x25,'audio-motor'),(0x20,'StreamCache'),(0x2b,'?dec2'),(0x29,'?dec0')]:
    dump_task(i,nm)
# mailbox 9 (decoder input) and exchange 6 (decoder->SC request)
MB=0x0897783c  # mailbox table base, stride 32
EX=0x0897fb1c  # exchange table base
print('== MAILBOX 9 (@%08x):'%(MB+9*32),' '.join('%08x'%(rd32(MB+9*32+4*j)or 0) for j in range(8)))
print('== MAILBOX 0x52(@%08x):'%(MB+0x52*32),' '.join('%08x'%(rd32(MB+0x52*32+4*j)or 0) for j in range(8)))
print('== EXCHANGE 6 (@%08x):'%(EX+6*32),' '.join('%08x'%(rd32(EX+6*32+4*j)or 0) for j in range(8)))
gdb.execute('detach');gdb.execute('quit')
