import gdb, time
gdb.execute('set pagination off'); gdb.execute('set confirm off')
gdb.execute('set tcp connect-timeout 60')
gdb.execute('target remote :1234')     # halts the guest on connect
print('ATTACHED/HALTED')
gdb.execute('info registers pc')
t0=time.time()
gdb.execute('dump binary memory /home/dmontero/6g/dram2.bin 0x08000000 0x0c000000')
print('DUMP DONE in %.1fs'%(time.time()-t0))
open('/home/dmontero/6g/HALTED2','w').write('1')
gdb.execute('detach'); gdb.execute('quit')
