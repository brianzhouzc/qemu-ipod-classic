import gdb
gdb.execute('set pagination off');gdb.execute('set confirm off')
gdb.execute('target remote :1234')
inf=gdb.selected_inferior()
def rd32(a):
    try: return int.from_bytes(inf.read_memory(a & 0xffffffff,4).tobytes(),'little')
    except: return None
def rdb(a):
    try: return inf.read_memory(a & 0xffffffff,1).tobytes()[0]
    except: return None
def code(v): return v is not None and (0x08000000<=v<0x08a00000 or 0x22000000<=v<0x2200c000)

DEC=0x0ba9a8a4            # decoder_obj
PORT=DEC+0x54            # actor port 0x0ba9a8f8
SCB=0x08911488          # actor-scheduler control block base
print('==== DECODER OBJECT %08x ===='%DEC)
print(' vtable[+0]   = %08x (expect 088d1948)'%(rd32(DEC) or 0))
g34=rd32(DEC+0x34); print(' [+0x34]      = %08x'%(g34 or 0))
if g34: print(' render-gate [[+0x34]+0x60] = %08x'%(rd32(g34+0x60) or 0))
print(' [+0x88]      = %08x'%(rd32(DEC+0x88) or 0))
print()
print('==== ACTOR PORT (obj+0x54) %08x ===='%PORT)
for off in (0,4,0x18,0x1c,0x20,0x24,0x28,0x2c):
    print('  port[+0x%02x] = %08x'%(off,rd32(PORT+off) or 0))
print()
print('==== SCHEDULER CB %08x ===='%SCB)
head=rd32(SCB+0)   ; print('  head[+0]       = %08x'%(head or 0))
cb=rd32(SCB+8)     ; print('  callback[+8]   = %08x'%(cb or 0))
semap=rd32(SCB+0x10);print('  sema-ptr[+0x10]= %08x'%(semap or 0))
p14=rd32(SCB+0x14) ; print('  [+0x14]        = %08x'%(p14 or 0))
mtx=rd32(SCB+0x18) ; print('  mutex[+0x18]   = %08x'%(mtx or 0))
print()
if semap:
    print('==== SEMAPHORE OBJECT *[+0x10] = %08x ===='%semap)
    cnt=rd32(semap+0); wtr=rd32(semap+4)
    print('  count[+0]  = %08x (%d)'%(cnt or 0, cnt if cnt is not None and cnt<0x80000000 else (cnt-0x100000000 if cnt else 0)))
    print('  waiter[+4] = %08x  <-- parked task TCB/SP'%(wtr or 0))
    for off in (8,0xc,0x10,0x14):
        print('  sema[+0x%02x]= %08x'%(off,rd32(semap+off) or 0))
print()
print('==== RUN-QUEUE walk from head ====')
n=head; seen=0
while n and seen<12:
    tag=rd32(n+0x18); qf=rd32(n+0x1c); nxt=rd32(n)
    isport = (n==PORT)
    print('  actor %08x  next=%08x tag[+0x18]=%08x queued[+0x1c]=%08x %s'%(n,nxt or 0,tag or 0,qf or 0,'<== decoder PORT' if isport else ''))
    n=nxt; seen+=1
if not head: print('  (run-queue EMPTY)')
print()

# ---- TCB scan: find the task whose savedSP matches sema waiter, dump all blocked ----
TCBb=0x0897ff70
RPC={0x2200433c:'op1-WAITsema',0x22004028:'op14-RECVmbox',0x22004170:'op4-RECVexch',0x22004394:'op1b-WAITsema',0x220040d8:'op5-SENDexch',0x22004360:'op34',0x22004224:'op25',0x22003d14:'op?',0x220157a0:'fastsema'}
print('==== TCB scan (56 tasks) ====')
waiter = rd32(semap+4) if semap else None
for i in range(0,56):
    tcb=TCBb+i*52; tn=rd32(tcb+32)
    if tn!=i: continue
    state=rd32(tcb+28)&0xff; sSP=rd32(tcb+16); rpc=rd32(sSP+56) if sSP else None
    entry=rd32(tcb+12)
    mark=''
    if waiter and sSP and (waiter==sSP or waiter==tcb or (waiter and abs(waiter-sSP)<0x400)): mark=' <<< SEMA WAITER?'
    lbl=RPC.get(rpc,'?')
    print('  t%02x st=%02x entry=%08x savedSP=%08x resumePC=%08x [%s]%s'%(i,state,entry or 0,sSP or 0,rpc or 0,lbl,mark))
gdb.execute('detach');gdb.execute('quit')
