set pagination off
set confirm off
target remote :1234
break *0x08150d84
continue
delete
printf "SM1pos_A=%u\n", *(unsigned int *)0x38500a48
break *0x22001ca4
set $n = 0
while $n < 70
  continue
  printf "ISRsig lr=0x%x obj=0x%x\n", $lr, $r0
  set $n = $n + 1
end
printf "SM1pos_B=%u\n", *(unsigned int *)0x38500a48
detach
quit
