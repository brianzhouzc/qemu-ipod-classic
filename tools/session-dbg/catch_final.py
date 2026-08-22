import gdb,sys
CURTCB=0x2200ac64; TCB_BASE=0x0897ff70; STRIDE=52
STATE=0x22008c4c
def r32(a):
    try: return int.from_bytes(gdb.selected_inferior().read_memory(a,4).tobytes(),'little')
    except: return None
def r8(a):
    try: return gdb.selected_inferior().read_memory(a,1).tobytes()[0]
    except: return None
def rg(n): return int(gdb.parse_and_eval('$'+n))&0xffffffff
def P(*a): print(*a);sys.stdout.flush()
gdb.execute('set pagination off');gdb.execute('set confirm off');gdb.execute('set mi-async off')
gdb.execute('target remote :1234')
P('attached pc=0x%x'%rg('pc'))
gdb.execute('break *0x08150d84')
gdb.execute('continue')
P('*** StartPlayback HIT pc=0x%x r0=0x%x r1=0x%x r2=0x%x r3=0x%x lr=0x%x task=0x%x ***'%(rg('pc'),rg('r0'),rg('r1'),rg('r2'),rg('r3'),rg('lr'),r8(r32(CURTCB)+32)))
gdb.execute('delete')
gdb.execute('break *0x0808170c')
gdb.execute('continue')
etask=r8(r32(CURTCB)+32)
ct=r8(STATE+0x58)
P('*** ENGINE HIT task=0x%x lr=0x%x  CODEC_TYPE[state+0x58]=%s  state+0x94=%s ***'%(etask,rg('lr'),hex(ct) if ct is not None else None, hex(r8(STATE+0x94) or 0)))
# dump state struct head
hd=gdb.selected_inferior().read_memory(STATE,0xa0).tobytes()
P('state@0x22008c4c: '+hd.hex())
gdb.execute('delete')
# follow engine task to its KS block
gdb.execute('break *0x2200079c')
n=0;done=False
while n<1200 and not done:
    gdb.execute('continue')
    if r8(r32(CURTCB)+32)==etask:
        cur=r32(CURTCB);ssp=r32(cur+16)
        P('*** ENGINE task blocks n=%d pc=0x%x r0=0x%x r1=0x%x r2=0x%x r3=0x%x ***'%(n,rg('pc'),rg('r0'),rg('r1'),rg('r2'),rg('r3')))
        done=True
    n+=1
P('follow n=%d done=%s'%(n,done))
gdb.execute('delete')
P('=== WAIT GRAPH ===')
cur=r32(CURTCB)
for i in range(0,0x40):
    tcb=TCB_BASE+i*STRIDE
    if r8(tcb+32)!=i: continue
    entry=r32(tcb+12);ssp=r32(tcb+16);st=r8(tcb+28);rpc=r32((ssp or 0)+56) if ssp else 0
    ch=[];bb=(ssp or 0)+64
    for j in range(0,240,4):
        w=r32(bb+j)
        if w and (0x08000000<=w<0x08400000 or 0x22000000<=w<0x2200c000): ch.append(w)
        if len(ch)>=5: break
    P('t%02x st=0x%02x entry=0x%08x rpc=0x%08x%s bt:%s'%(i,st or 0,entry or 0,rpc or 0,' CUR' if tcb==cur else '',' '.join('%08x'%c for c in ch)))
gdb.execute('detach');gdb.execute('quit')
