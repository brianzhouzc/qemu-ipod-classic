import gdb
gdb.execute('set pagination off');gdb.execute('set confirm off')
gdb.execute('target remote :1234')
inf=gdb.selected_inferior()
def rd32(a):
    try: return int.from_bytes(inf.read_memory(a & 0xffffffff,4).tobytes(),'little')
    except: return None
DEC=0x0ba9a8a4; PORT=DEC+0x54; SCB=0x08911488; SEMA=0x08970050
print('== DECODER %08x vtable=%08x [+0x34]=%08x gate[[+34]+60]=%08x'%(DEC,rd32(DEC) or 0,rd32(DEC+0x34) or 0,(rd32((rd32(DEC+0x34) or 0)+0x60) or 0)))
print('== PORT %08x  msgcode[+4]=%08x tag[+0x18]=%08x pend[+0x20]=%08x queued[+0x1c]=%08x next[+0]=%08x'%(PORT,rd32(PORT+4) or 0,rd32(PORT+0x18) or 0,rd32(PORT+0x20) or 0,rd32(PORT+0x1c) or 0,rd32(PORT) or 0))
head=rd32(SCB);sem=rd32(SCB+0x10)
print('== SCB head=%08x sema-ptr=%08x'%(head or 0,sem or 0))
if sem: print('== SEMA %08x count[+0]=%08x waiter[+4]=%08x'%(sem,rd32(sem) or 0,rd32(sem+4) or 0))
# run-queue: is PORT present?
n=head;seen=0;found=False
while n and seen<40:
    if n==PORT: found=True
    n=rd32(n);seen+=1
print('== RUNQ len(capped40)=%d  decoderPORT_in_queue=%s'%(seen,found))
# find op1-WAITsema task parked on SEMA 0x08970050: scan each task frame for the sema addr
TCBb=0x0897ff70
print('== scan op1-WAITsema tasks for reference to SEMA %08x =='%SEMA)
for i in range(56):
    tcb=TCBb+i*52; tn=rd32(tcb+32)
    if tn!=i: continue
    sSP=rd32(tcb+16); rpc=rd32(sSP+56) if sSP else None
    if rpc not in (0x2200433c,0x22004394): continue
    hit=[]
    for k in range(0,72):
        v=rd32(sSP+4*k)
        if v==SEMA: hit.append(4*k)
        if v==SCB or v==SCB+0x10: hit.append(('SCB',4*k))
    if hit:
        print('   t%02x sSP=%08x rpc=%08x  refs=%s'%(i,sSP,rpc,hit))
gdb.execute('detach');gdb.execute('quit')
