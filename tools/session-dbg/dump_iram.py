import gdb
gdb.execute('set pagination off');gdb.execute('set confirm off')
gdb.execute('target remote :1234')
inf=gdb.selected_inferior()
data=inf.read_memory(0x22000000,0xc000).tobytes()
open('/home/dmontero/6g/iram.bin','wb').write(data)
print('IRAM dumped %d bytes, pc=0x%x'%(len(data),int(gdb.parse_and_eval('$pc'))&0xffffffff))
gdb.execute('detach');gdb.execute('quit')
