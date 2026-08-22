set pagination off
set confirm off
target remote :1234
printf "PC=%x cpsr=%x\n", $pc, $cpsr
echo --- REGIME ---\n
printf "ex6[fb34]=%08x  M52.rx=%08x M52.seq=%08x  M09.rx=%08x M09.seq=%08x\n", *(unsigned int*)0x0897fb34, *(unsigned int*)0x08978284, *(unsigned int*)0x08978294, *(unsigned int*)0x08977964, *(unsigned int*)0x08977974
echo --- VIC0 (irq16=DMAC0, irq29=ATA) ---\n
printf "VIC0 IRQStatus=%08x RawIntr=%08x IntEnable=%08x  (bit16=%d bit29=%d raw16=%d raw29=%d)\n", *(unsigned int*)0x38E00000, *(unsigned int*)0x38E00008, *(unsigned int*)0x38E00010, (*(unsigned int*)0x38E00000>>16)&1, (*(unsigned int*)0x38E00000>>29)&1, (*(unsigned int*)0x38E00008>>16)&1, (*(unsigned int*)0x38E00008>>29)&1
echo --- DMAC0 globals ---\n
printf "DMAC0 Config=%x IntStatus=%x IntTCStatus=%x RawIntTC=%x ErrStatus=%x EnbldChns=%x\n", *(unsigned int*)0x38200030, *(unsigned int*)0x38200000, *(unsigned int*)0x38200004, *(unsigned int*)0x38200014, *(unsigned int*)0x3820000c, *(unsigned int*)0x3820001c
set $c=0
while $c<2
  set $b=0x38200100+$c*0x20
  printf "  DMAC0 ch%d SRC=%08x DEST=%08x LLI=%08x CTRL=%08x CONF=%08x (E=%d size=%d)\n", $c, *(unsigned int*)$b, *(unsigned int*)($b+4), *(unsigned int*)($b+8), *(unsigned int*)($b+0xc), *(unsigned int*)($b+0x10), *(unsigned int*)($b+0x10)&1, *(unsigned int*)($b+0xc)&0xfff
  set $c=$c+1
end
echo --- ATA (base 0x38700000) ---\n
printf "ATA STATUS(+04)=%x IRQSTAT(+10)=%x IRQMASK(+14)=%x XFR_NUM(+34)=%x XFR_CNT(+38)=%x SBUF(+44)=%x CADR_SBUF(+50)=%x\n", *(unsigned int*)0x38700004, *(unsigned int*)0x38700010, *(unsigned int*)0x38700014, *(unsigned int*)0x38700034, *(unsigned int*)0x38700038, *(unsigned int*)0x38700044, *(unsigned int*)0x38700050
echo --- TIMERS (base 0x3C700000; A-D +0x00 stride 0x20 share IRQ8; E-H +0xA0 share IRQ7) CON CMD DATA0 PRE ---\n
set $t=0
while $t<8
  set $b=0x3C700000
  if $t<4
    set $b=0x3C700000+$t*0x20
  end
  if $t>=4
    set $b=0x3C7000A0+($t-4)*0x20
  end
  printf "  T%d @%x CON=%08x CMD=%x DATA0=%x PRE=%x  (EN=%d INT_EN=%x CS=%d ECLKbit=%d)\n", $t, $b, *(unsigned int*)$b, *(unsigned int*)($b+4), *(unsigned int*)($b+8), *(unsigned int*)($b+0x10), *(unsigned int*)($b+4)&1, (*(unsigned int*)$b>>12)&7, (*(unsigned int*)$b>>8)&7, (*(unsigned int*)$b>>6)&1
  set $t=$t+1
end
echo --- SM1 audio pos (should still be advancing HW) ---\n
printf "SM1 POS(+a48)=%x RUN(+400)=%x STAT(+a98)=%x\n", *(unsigned int*)0x38500a48, *(unsigned int*)0x38500400, *(unsigned int*)0x38500a98
detach
quit
