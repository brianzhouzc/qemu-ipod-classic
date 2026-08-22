import gdb, time
gdb.execute('set pagination off'); gdb.execute('set confirm off')
gdb.execute('set tcp connect-timeout 60')
gdb.execute('target remote :1234')
gdb.execute('break *0x081e9e70')      # innerdec (AAC math)
gdb.execute('break *0x081e93d8')      # rpull (reader-pull)
gdb.execute('break *0x0808170c')      # engine
open('/home/dmontero/6g/GDBREADY','w').write('1')
print('GDB armed, continuing...')
gdb.execute('continue')               # blocks until a breakpoint hits -> guest HALTED
print('HALTED')
gdb.execute('info registers pc r0 r1 r2 r3')
t0=time.time()
gdb.execute('dump binary memory /home/dmontero/6g/dram.bin 0x08000000 0x0c000000')
print('DUMP DONE in %.1fs'%(time.time()-t0))
open('/home/dmontero/6g/HALTED','w').write('1')
gdb.execute('detach'); gdb.execute('quit')
