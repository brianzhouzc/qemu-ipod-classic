set pagination off
set confirm off
target remote :1234
break *0x08150d84
continue
printf "=== StartPlayback pc=%x ===\n", $pc
delete
break *0x0808170c
continue
set $etask = *(unsigned char *)(*(unsigned int *)0x2200ac64 + 32)
printf "=== ENGINE runs in task 0x%x lr=%x sp=%x ===\n", $etask, $lr, $sp
printf "engine caller stack:\n"
x/28xw $sp
delete
break *0x2200079c
set $n = 0
set $done = 0
while $n < 500 && $done == 0
  continue
  set $tn = *(unsigned char *)(*(unsigned int *)0x2200ac64 + 32)
  if $tn == $etask
    set $ssp = *(unsigned int *)(*(unsigned int *)0x2200ac64 + 16)
    printf "*** ENGINE task 0x%x BLOCKS sem=0x%x resumePC=0x%x (n=%d) ***\n", $tn, $fp, *(unsigned int *)($ssp+56), $n
    printf "block stack:\n"
    x/24xw $ssp+64
    set $done = 1
  end
  set $n = $n + 1
end
printf "done follow n=%d done=%d\n", $n, $done
detach
quit
