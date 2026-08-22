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
DEC=0x0ba9a8a4; ENG=0x220106dc; RING=0x220106d0
ctx=rd32(DEC+0x34); print('ctx=%s'%h(ctx))
reader=rd32((ctx or 0)+0x48); print('reader=%s vtable=%s'%(h(reader),h(rd32(reader or 0))))
# reader ring buffer descriptor [reader+4] -> head[+60] tail[+64]; input gate [[reader+48]+0x2b8]
rd=rd32((reader or 0)+4)
print('  reader[+4]ringdesc=%s'%h(rd))
if rd: print('     ring head[+60]=%s tail[+64]=%s [+56]=%s'%(h(rd32(rd+60)),h(rd32(rd+64)),h(rd32(rd+56))))
print('  reader[+8]=%s [+28]=%s'%(h(rd32((reader or 0)+8)), h(rd8((reader or 0)+28))))
g48=rd32((reader or 0)+0x30)
print('  reader[+48]=%s  input-gate[[+48]+0x2b8]=%s'%(h(g48), h(rd32((g48 or 0)+0x2b8))))
# mixer PCM source 0x08889098 first words (should be PCM if decode published)
print('  mixer-src 0x08889098: '+' '.join(h(rd32(0x08889098+4*j)) for j in range(6)))
# output descriptor 0x0bb12d54 gate [+5]
print('  descr 0x0bb12d54 [+5]gate=%s  0x0bb12eb4 [+5]=%s'%(h(rd8(0x0bb12d54+5)), h(rd8(0x0bb12eb4+5))))
print('  descr 0x0bb12d54 words: '+' '.join(h(rd32(0x0bb12d54+4*j)) for j in range(4)))
# engine
print('  ENGOBJ [+70]=%02x [+71]gate=%02x [+88]type=%02x  RING[+4]waiter=%s'%(rd8(ENG+70)or 0,rd8(ENG+71)or 0,rd8(ENG+88)or 0,h(rd32(RING+4))))
# motor data-pump source: engobj has a source ptr somewhere; scan engobj+0..+68 for heap ptrs
print('  ENGOBJ words +0..+64:')
for r in range(0,68,16):
    print('    +%3d:'%r,' '.join(h(rd32(ENG+r+4*j)) for j in range(4)))
gdb.execute('detach'); gdb.execute('quit')
