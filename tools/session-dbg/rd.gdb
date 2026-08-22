set pagination off
set confirm off
target remote :1234
echo \n===== CODEC STATE (state=0x220106dc) =====\n
printf "TYPE  [state+0x58 @0x22010734] = 0x%02x (signed %d)\n", *(unsigned char*)0x22010734, *(char*)0x22010734
printf "GATE47[state+0x47 @0x22010723] = 0x%02x\n", *(unsigned char*)0x22010723
printf "b48   [state+0x48 @0x22010724] = 0x%02x\n", *(unsigned char*)0x22010724
echo --- state head ---\n
x/28xw 0x220106dc
echo \n===== WAIT GRAPH =====\n
echo exchange6 slot @0x0897fb34 (0=signalled 0x20=t20-wait 0xffffffff=unalloc):\n
x/1xw 0x0897fb34
echo mbox0x52 @0x0897827c (w0=owner w2=rx-tcb w6=seq):\n
x/8xw 0x0897827c
echo mbox0x09 @0x0897795c:\n
x/8xw 0x0897795c
echo \n===== dump IRAM 0x22010000-0x22011000 =====\n
dump binary memory /home/dmontero/6g/iram2.bin 0x22000000 0x22020000
detach
quit
