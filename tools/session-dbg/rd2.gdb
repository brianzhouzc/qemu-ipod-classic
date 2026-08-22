set pagination off
set confirm off
target remote :1234
printf "REGIME: TYPE=0x%02x GATE47=0x%02x  ", *(unsigned char*)0x22010734, *(unsigned char*)0x22010723
printf "ex6@fb34=0x%08x  ", *(unsigned int*)0x0897fb34
printf "M52.w2rx=0x%08x M52.w6seq=0x%08x  ", *(unsigned int*)0x08978284, *(unsigned int*)0x08978294
printf "M09.w6seq=0x%08x\n", *(unsigned int*)0x08977974
detach
quit
