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
h=lambda v: ('%08x'%v) if v is not None else 'None'
def dump(a,n,label):
    print('  '+label+' @'+h(a)+': '+' '.join(h(rd32(a+4*j)) for j in range(n)))
print('==== T25 RENDER-POOL (r5=0x22008c10) ====')
POOL=0x22008c10
dump(POOL,8,'pool')
print('  pool[+0]state='+h(rd8(POOL))+' [+4]srcHolder='+h(rd32(POOL+4))+' [+12]counter(8c1c)='+h(rd32(POOL+12)))
holder=rd32(POOL+4)
print('==== SOURCE POLL CHAIN (bl 0x22005e48 = bx [[[holder]]+28]) ====')
print('  srcHolder='+h(holder))
if holder:
    S=rd32(holder)
    print('  [holder] -> S(source obj)='+h(S))
    if S:
        V=rd32(S)
        print('  [S] -> vtable V='+h(V))
        dump(S,16,'SRCOBJ')
        if V:
            print('  --- vtable methods ---')
            for off,nm in ((8,'m08'),(12,'m12'),(24,'m18'),(28,'POLL+28'),(32,'m20'),(44,'m2c'),(56,'m38')):
                print('    V+'+str(off)+' '+nm+' = '+h(rd32(V+off)))
            dump(V,20,'VTABLE')
print('==== PING-PONG ====')
dump(0x22008c5c,4,'pp0_8c5c'); dump(0x22009c5c,4,'pp1_9c5c')
print('==== SINK DESCRIPTORS ====')
dump(0x0bb12d54,10,'copyDescr_bb12d54')
print('    copyDescr[+0]fmt='+h(rd8(0x0bb12d54))+' [+5]='+h(rd8(0x0bb12d54+5))+' [+21]='+h(rd8(0x0bb12d54+21)))
dump(0x0bb12eb4,10,'gateDescr_bb12eb4')
print('    gateDescr[+5]GATE='+h(rd8(0x0bb12eb4+5)))
print('==== MIXER SOURCE BUF ====')
dump(0x08889098,8,'srcbuf_8889098')
gdb.execute('detach'); gdb.execute('quit')
