import gdb
gdb.execute('set pagination off'); gdb.execute('set confirm off')
gdb.execute('target remote :1234')
inf=gdb.selected_inferior()
def rd32(a):
    try: return int.from_bytes(inf.read_memory(a&0xffffffff,4).tobytes(),'little')
    except: return None
def rd8(a):
    try: return inf.read_memory(a&0xffffffff,1).tobytes()[0]
    except: return None
def code(v): return v is not None and (0x08000000<=v<0x08a00000 or 0x22000000<=v<0x2200c000)
DEC=0x0ba9a8a4; TCBB=0x0897ff70
RING=0x220106d0; ENG=0x220106dc
h=lambda v: ('%08x'%v) if v is not None else 'None'
print('== DECODER obj=%08x vtable=%s'%(DEC,h(rd32(DEC))))
ctx=rd32(DEC+0x34)
print('   [+0x34]ctx=%s [+0x38]=%s(=obj56) [+0x4c]=%s(obj76) [+0x50]inflag=%02x [+0x84]=%s(obj132)'%(
    h(ctx), h(rd32(DEC+0x38)), h(rd32(DEC+0x4c)), (rd8(DEC+0x50)or 0), h(rd32(DEC+0x84))))
print('   DECODE-GATE reach 0x8123390 iff ctx!=0 AND [obj132]==0 AND [obj56]==0')
if ctx:
    cv=rd32(ctx)
    print('   ctx=%08x vtable=%s [ctx+0x30]=%s(=48) [ctx+0x48]reader=%s(=72) gate[ctx+0x60]=%s'%(
        ctx, h(cv), h(rd32(ctx+0x30)), h(rd32(ctx+0x48)), h(rd32(ctx+0x60))))
    if cv:
        print('   decode-method [ctxvt+0x68=+104]=%s (expect 081c0a1c)'%h(rd32(cv+0x68)))
    reader=rd32(ctx+0x48)
    if reader:
        rv=rd32(reader)
        print('   reader=%08x vtable=%s pull[rvt+16]=%s'%(reader, h(rv), h(rd32((rv or 0)+16))))
# tasks
RPC={0x2200433c:'op1-WAITsema',0x22004028:'op14-RECVmbox',0x22004170:'op4-RECVexch',
     0x22004394:'op1b-WAITsema',0x220040d8:'op5-SENDexch',0x22004360:'op34'}
for name,tn in (('t20-SC',0x20),('t25-motor',0x25),('t2a-dec',0x2a)):
    tcb=TCBB+tn*52; sSP=rd32(tcb+16); state=rd8(tcb+28)
    rpc=rd32((sSP or 0)+56) if sSP else 0
    print('== %s state=%02x savedSP=%s resumePC=%s [%s]'%(name,state or 0,h(sSP),h(rpc),RPC.get(rpc or 0,'?')))
    if sSP:
        rets=[]
        for k in range(0,240):
            v=rd32(sSP+4*k)
            if code(v):
                pi=rd32(v-4)
                if pi is not None and ((pi&0x0f000000)==0x0b000000 or (pi&0xfe000000)==0xfa000000):
                    rets.append('%08x'%v)
            if len(rets)>=10: break
        print('     RET:',' '.join(rets))
ef=lambda o: rd8(ENG+o) or 0
print('== ENGOBJ [+70]=%02x [+71]gate=%02x [+88]type=%02x  RING[+4]waiter=%s'%(ef(70),ef(71),ef(88),h(rd32(RING+4))))

# --- producer/consumer binding evidence ---
reader=rd32((ctx or 0)+0x48)
if reader:
    g48=rd32(reader+0x30)
    print('  reader=%s [+4]ring=%s decode-gate[[+48]+0x2b8]=%s'%(h(reader),h(rd32(reader+4)),h(rd32((g48 or 0)+0x2b8))))
print('  motor copy-descr 0x0bb12d54: '+' '.join(h(rd32(0x0bb12d54+4*j)) for j in range(4))+'  [+5]=%s'%h(rd8(0x0bb12d59)))
print('  mixer-src 0x08889098: '+' '.join(h(rd32(0x08889098+4*j)) for j in range(4)))
gdb.execute('detach'); gdb.execute('quit')
