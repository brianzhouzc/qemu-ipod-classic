# How much work does the audio decoder actually have queued?
#
# The frame pull 0x081e93d8, once past its credit gate, runs a loop:
#     r6 = [obj+0x3c]                     (producer index)
#     loop: ... bl 0x081e9e70 (codec math) ; r6--
#     until [obj+0x40] == r6              (consumer index)
# with obj = [reader+4]. Measured aacmath=1, so the loop body ran exactly once:
# the decoder consumed the single unit available and returned. That is INPUT
# STARVATION, not a blocked decoder -- so the question is who advances +0x3c.
#
# Break just before the loop and print both indices plus the reader, so the queue
# depth is a number instead of an inference.
set pagination off
set confirm off
set $__q = 0
target remote :1234
break *0x081e9428
commands
  silent
  set $__q = $__q + 1
  set $obj = *(unsigned int *)($r4 + 4)
  printf "QUEUE #%d reader=0x%08x obj=0x%08x  prod[+0x3c]=%d  cons[+0x40]=%d  depth=%d\n", \
         $__q, $r4, $obj, *(int *)($obj + 0x3c), *(int *)($obj + 0x40), \
         (*(int *)($obj + 0x3c)) - (*(int *)($obj + 0x40))
  printf "   [reader+8]=0x%08x  [obj+0x38]=%d  [obj+0x44]=%d\n", \
         *(unsigned int *)($r4 + 8), *(int *)($obj + 0x38), *(int *)($obj + 0x44)
  if $__q >= 8
    detach
    quit
  end
  continue
end
printf "queue probe armed at 0x081e9428\n"
continue
