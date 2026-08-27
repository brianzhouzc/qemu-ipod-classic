# Where does the codec actually WRITE its samples?
#
# Established: the codec math 0x081e9e70 runs, but the mixer's source ring
# (0x0888a2xx-0x0888a6xx in a real play) only ever receives memset. So the codec's
# output buffer is somewhere else and the link to the mixer is what is missing.
#
# Break at the codec entry, dump its arguments and the reader object, and look for
# the output pointer. r0=reader, r1=index, r2=flag, r3=count.
set pagination off
set confirm off
set $__k = 0
target remote :1234
break *0x081e9e70
commands
  silent
  set $__k = $__k + 1
  printf "CODEC #%d r0=0x%08x r1=0x%08x r2=0x%08x r3=%d\n", $__k, $r0, $r1, $r2, $r3
  if $__k <= 2
    printf "  reader object at 0x%08x:\n", $r0
    set $i = 0
    while $i < 16
      printf "    +0x%02x = 0x%08x\n", $i*4, *(unsigned int *)($r0 + $i*4)
      set $i = $i + 1
    end
    set $obj = *(unsigned int *)($r0 + 4)
    printf "  obj at 0x%08x:\n", $obj
    set $i = 12
    while $i < 20
      printf "    +0x%02x = 0x%08x\n", $i*4, *(unsigned int *)($obj + $i*4)
      set $i = $i + 1
    end
  end
  if $__k >= 4
    detach
    quit
  end
  continue
end
printf "codec probe armed at 0x081e9e70\n"
continue
