set pagination off
set confirm off
target remote :1234
# after the reader-method call in publish: r0 = frame object (or 0)
break *0x081c12e8
set $n=0
while $n<6
  continue
  printf "PUB-RET #%d r0(frame)=%08x\n", $n, $r0
  if $r0 >= 0x08000000
    printf "  frame[0..0x40]: "
    set $k=0
    while $k<16
      printf "%08x ", *(unsigned int*)($r0+4*$k)
      set $k=$k+1
    end
    printf "\n"
  end
  set $n=$n+1
end
# also break at the post branch to see [r0]
delete
break *0x081c1304
set $m=0
while $m<3
  continue
  printf "PUB-POST #%d r0=%08x [r0]=%08x [r0+4]=%08x [r0+8]=%08x [r0+c]=%08x\n", $m, $r0, *(unsigned int*)$r0, *(unsigned int*)($r0+4), *(unsigned int*)($r0+8), *(unsigned int*)($r0+12)
  set $m=$m+1
end
detach
quit
