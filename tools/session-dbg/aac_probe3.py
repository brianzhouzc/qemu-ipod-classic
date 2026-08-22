import gdb
gdb.execute('set pagination off');gdb.execute('set confirm off')
gdb.execute('target remote :1234')
inf=gdb.selected_inferior()
def rd32(a):
    try: return int.from_bytes(inf.read_memory(a&0xffffffff,4).tobytes(),'little')
    except: return None
def code(v): return v is not None and (0x08000000<=v<0x08a00000 or 0x22000000<=v<0x2200c000)
DEC=0x0ba9a8a4;PORT=DEC+0x54;SCB=0x08911488;SEMA=0x08970050;MTX=0x08970060
print('== gate[[+34]+60]=%08x  PORT queued[+1c]=%08x msg[+4]=%08x tag[+18]=%08x'%((rd32((rd32(DEC+0x34)or 0)+0x60)or 0),rd32(PORT+0x1c)or 0,rd32(PORT+4)or 0,rd32(PORT+0x18)or 0))
head=rd32(SCB);n=head;seen=0;found=False;chain=[]
while n and seen<40:
    chain.append(n);
    if n==PORT:found=True
    n=rd32(n);seen+=1
print('== RUNQ len=%d decoderPORT_in_queue=%s head=%08x'%(seen,found,head or 0))
print('== SEMA %08x:'%SEMA,' '.join('%08x'%(rd32(SEMA+4*j)or 0) for j in range(8)))
print('== MUTEX %08x:'%MTX,' '.join('%08x'%(rd32(MTX+4*j)or 0) for j in range(8)))
# t04 unwind
TCBb=0x0897ff70;i=4;tcb=TCBb+i*52
sSP=rd32(tcb+16);state=rd32(tcb+28)&0xff;rpc=rd32(sSP+56)
print('== t04 state=%02x savedSP=%08x resumePC=%08x =='%(state,sSP,rpc))
print('   saved ctx [sSP..+64]:')
for r in range(0,68,16):
    print('    +%3d:'%r,' '.join('%08x'%(rd32(sSP+r+4*j)or 0) for j in range(4)))
print('   --- OSOS/IRAM return-address chain on t04 stack ---')
for k in range(0,200):
    a=sSP+4*k;v=rd32(a)
    if code(v):
        pi=rd32(v-4)
        isbl=pi is not None and ((pi&0x0f000000)==0x0b000000 or (pi&0xfe000000)==0xfa000000)
        if isbl: print('     [sSP+%4d]=%08x  <-RET'%(4*k,v))
gdb.execute('detach');gdb.execute('quit')
