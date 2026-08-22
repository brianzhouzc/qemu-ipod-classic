set pagination off
set confirm off
target remote :1234
dump binary memory /home/dmontero/6g/iramfull.bin 0x22000000 0x22008000
detach
quit
