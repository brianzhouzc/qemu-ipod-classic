import gdb,sys
CURTCB=0x2200ac64; TCB_BASE=0x0897ff70; STRIDE=52
MBX=0x0897783c; SEMA=0x0897fb1c
def rd32(a):
    try: return int.from_bytes(gdb.selected_inferior().read_memory(a,4).tobytes(),'little')
    except: return None
def rd8(a):
    try: return gdb.selected_inferior().read_memory(a,1).tobytes()[0]
    except: return None
def reg(n): return int(gdb.parse_and_eval('$'+n))&0xffffffff
def P(*a): print(*a);sys.stdout.flush()
gdb.execute('set pagination off');gdb.execute('set confirm off');gdb.execute('set target-async off')
gdb.execute('target remote :1234')
P('=== attached, waiting for audio_engine 0x0808170c ===')
gdb.execute('break *0x0808170c')
gdb.execute('continue')
etask=rd8(rd32(CURTCB)+32)
P('ENGINE runs in task 0x%x pc=0x%x lr=0x%x'%(etask,reg('pc'),reg('lr')))
gdb.execute('delete')
# follow to KS block point 0x2200079c until engine task blocks
gdb.execute('break *0x2200079c')
n=0;done=False
while n<800 and not done:
    gdb.execute('continue')
    tn=rd8(rd32(CURTCB)+32)
    if tn==etask:
        cur=rd32(CURTCB); ssp=rd32(cur+16)
        P('*** ENGINE task 0x%x at KS block n=%d pc=0x%x r0=0x%x r1=0x%x r2=0x%x ***'%(tn,n,reg('pc'),reg('r0'),reg('r1'),reg('r2')))
        done=True
    n+=1
P('follow done n=%d done=%s'%(n,done))
gdb.execute('delete')
# enumerate all tasks
P('=== WAIT GRAPH (56 TCB) ===')
cur=rd32(CURTCB); curtn=rd8(cur+32)
for i in range(0,0x40):
    tcb=TCB_BASE+i*STRIDE; tn=rd8(tcb+32)
    if tn!=i: continue
    entry=rd32(tcb+12); ssp=rd32(tcb+16); state=rd8(tcb+28)
    rpc=rd32((ssp or 0)+56) if ssp else 0
    chain=[]
    base=(ssp or 0)+64
    for j in range(0,300,4):
        w=rd32(base+j)
        if w and (0x08000000<=w<0x08400000 or 0x22000000<=w<0x2200c000):
            chain.append(w)
        if len(chain)>=6: break
    flag=' CUR' if tcb==cur else ''
    P('t%02x st=0x%02x entry=0x%08x rpc=0x%08x%s bt:%s'%(i,state or 0,entry or 0,rpc or 0,flag,' '.join('%08x'%c for c in chain)))
gdb.execute('detach');gdb.execute('quit')
