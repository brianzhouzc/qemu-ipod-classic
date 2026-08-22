import gdb
gdb.execute('set pagination off');gdb.execute('set confirm off')
gdb.execute('target remote :1234')
inf=gdb.selected_inferior()
def rd32(a):
    try: return int.from_bytes(inf.read_memory(a&0xffffffff,4).tobytes(),'little')
    except: return None
DEC=0x0ba9a8a4;PORT=DEC+0x54;SCB=0x08911488
print('== decoder PORT %08x TIMER fields:'%PORT)
for off,nm in [(4,'interval[+4]'),(8,'deadline[+8]'),(0xc,'[+c]'),(0x10,'[+10]'),(0x14,'callback[+14]'),(0x18,'tag[+18]'),(0x1c,'queued[+1c]'),(0x20,'pendtag[+20]'),(0x24,'ctx[+24]'),(0x28,'[+28]')]:
    print('   %-14s = %08x'%(nm,rd32(PORT+off) or 0))
# every entry in the timer wheel: interval, deadline, callback, tag
head=rd32(SCB);n=head;seen=0
print('== timer-wheel entries (deadline / interval / callback / tag):')
while n and seen<12:
    print('   %08x: dl[+8]=%08x iv[+4]=%08x cb[+14]=%08x tag=%08x ctx[+24]=%08x'%(n,rd32(n+8)or 0,rd32(n+4)or 0,rd32(n+0x14)or 0,rd32(n+0x18)or 0,rd32(n+0x24)or 0))
    n=rd32(n);seen+=1
gdb.execute('detach');gdb.execute('quit')
