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

RING=0x220106d0; ENG=0x220106dc; DEC=0x0ba9a8a4
TCBB=0x0897ff70
print('== RING 0x%08x: '%RING+' '.join('[+%d]=%08x'%(o,rd32(RING+o)or 0) for o in (0,4,8,12,16)))
print('== ENGOBJ 0x%08x: [+70]=%02x [+71]=%02x (render gate)'%(ENG,rd8(ENG+70)or 0,rd8(ENG+71)or 0))
dctx=rd32(DEC+0x34)
print('== DECODER gate [[+34]+60]=%08x'%((rd32((dctx or 0)+0x60)or 0)))
for name,tn in (('t20',0x20),('t25',0x25),('t2a',0x2a)):
    tcb=TCBB+tn*52
    entry=rd32(tcb+12); sSP=rd32(tcb+16); state=rd8(tcb+28); tasknum=rd32(tcb+32)
    rpc=rd32((sSP or 0)+56) if sSP else 0
    print('== %s tcb=%08x tasknum=%02x state=%02x savedSP=%08x resumePC=%08x'%(name,tcb,tasknum or 0,state or 0,sSP or 0,rpc or 0))
    if sSP:
        rets=[]
        for k in range(0,220):
            v=rd32(sSP+4*k)
            if code(v):
                pi=rd32(v-4)
                isbl=pi is not None and ((pi&0x0f000000)==0x0b000000 or (pi&0xfe000000)==0xfa000000)
                if isbl: rets.append('%08x'%v)
            if len(rets)>=8: break
        print('     RET chain:',' '.join(rets))
gdb.execute('detach'); gdb.execute('quit')
