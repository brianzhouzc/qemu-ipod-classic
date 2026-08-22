set pagination off
set confirm off
target remote :1234
echo === block point 0x2200079c ===\n
x/40i 0x2200078c
echo === recv paths ===\n
x/12i 0x22004020
x/12i 0x22004168
x/12i 0x22004330
detach
quit
