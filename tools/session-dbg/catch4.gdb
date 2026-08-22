set pagination off
set confirm off
target remote :1234
printf "PC=%x\n", $pc
printf "I2S: CLKCON=%x TXCON=%x TXCOM=%x CLKDIV=%x STATUS=%x\n", *(unsigned int *)0x3ca00000, *(unsigned int *)0x3ca00004, *(unsigned int *)0x3ca00008, *(unsigned int *)0x3ca00040, *(unsigned int *)0x3ca0003c
printf "DMAC0 CONFIG=%x\n", *(unsigned int *)0x38200030
set $c = 0
while $c < 8
  set $b = 0x38200100 + $c*0x20
  printf "  DMAC0 ch%d SRC=%x DEST=%x LLI=%x CTRL=%x CONF=%x\n", $c, *(unsigned int *)$b, *(unsigned int *)($b+4), *(unsigned int *)($b+8), *(unsigned int *)($b+0xc), *(unsigned int *)($b+0x10)
  set $c = $c + 1
end
printf "DMAC1 CONFIG=%x\n", *(unsigned int *)0x39900030
set $c = 0
while $c < 8
  set $b = 0x39900100 + $c*0x20
  printf "  DMAC1 ch%d SRC=%x DEST=%x LLI=%x CTRL=%x CONF=%x\n", $c, *(unsigned int *)$b, *(unsigned int *)($b+4), *(unsigned int *)($b+8), *(unsigned int *)($b+0xc), *(unsigned int *)($b+0x10)
  set $c = $c + 1
end
printf "SM1: RUN(+400)=%x STAT(+a98)=%x POS(+a48)=%x +404=%x +860=%x\n", *(unsigned int *)0x38500400, *(unsigned int *)0x38500a98, *(unsigned int *)0x38500a48, *(unsigned int *)0x38500404, *(unsigned int *)0x38500860
detach
quit
