# Who owns the mixer's SOURCE object during a real play?
#
# At the copy's halfword path (0x08016edc) the live registers are:
#   r0 = source pointer (measured 0x0888a2xx-0x0888a6xx)
#   r4 = the copy descriptor
#   r5 = the source descriptor
# The copy reads its samples from [r5+12] + offset. Dump r5's fields and the bytes
# at the source pointer: that says whether the buffer holds silence or samples, and
# gives the object we have to trace back to whoever should fill it.
set pagination off
set confirm off
set $__s = 0
target remote :1234
break *0x08016edc
commands
  silent
  set $__s = $__s + 1
  if $__s <= 3
    printf "SRC #%d  r0=0x%08x r4=0x%08x r5=0x%08x\n", $__s, $r0, $r4, $r5
    printf "  src descriptor at 0x%08x:\n", $r5
    set $i = 0
    while $i < 10
      printf "    +0x%02x = 0x%08x\n", $i*4, *(unsigned int *)($r5 + $i*4)
      set $i = $i + 1
    end
    printf "  first samples at src 0x%08x: %08x %08x %08x %08x\n", $r0, \
      *(unsigned int *)($r0), *(unsigned int *)($r0+4), \
      *(unsigned int *)($r0+8), *(unsigned int *)($r0+12)
    printf "  copy descriptor [r4+12]=0x%08x [r4+16]=%d [r4+21]=%d\n", \
      *(unsigned int *)($r4+12), *(int *)($r4+16), *(unsigned char *)($r4+21)
  end
  if $__s >= 6
    detach
    quit
  end
  continue
end
printf "source-object probe armed at 0x08016edc\n"
continue
