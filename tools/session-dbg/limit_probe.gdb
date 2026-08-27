# What bounds how much the decoder is allowed to produce?
#
# The queue depth that framepull's loop iterates over is set by 0x81c08bc, and the
# disassembly says it comes from exactly two fields of [obj+0x30]:
#
#   81c08e0: ldr r3,[r2,#0x2b4]   <- current position
#   81c08e4: ldr r2,[r2,#0x2b8]   <- the LIMIT
#   81c08e8: cmp r3,r0            ; r0 = [obj+0x34] >> 1
#   81c08f0: add r0,r3,r0
#   81c08f4: cmp r0,r2
#   81c08f8: movlt r5,r0          <- inside the limit: take position + half
#   81c0900: cmp r2,#0
#   81c0904: subne r5,r2,#1       <- otherwise clamp to limit-1
#
# So if 0x2b8 is small, the window is small and the codec runs once no matter what
# else is right. 0x81c08b4 is the load of 0x2b4 with r1 already holding the object,
# so both fields are readable there, and so is the halfsize the window is built
# from.
set pagination off
set confirm off
set $__l = 0
target remote :1234
break *0x081c08b4
commands
  silent
  set $__l = $__l + 1
  # Sample SPARSELY and keep going. The first version stopped after ten hits, and
  # every one of them landed in the first milliseconds of the run -- which is why
  # it reported the position as a flat zero: it was reading before playback had
  # started. Powers of two out to 4096 cover start-up and steady state both.
  if $__l == 1 || $__l == 8 || $__l == 32 || $__l == 128 || $__l == 512 || $__l == 2048 || $__l == 4096
    printf "limit#%-5d  obj=0x%08x  pos[+0x2b4]=%d  limit[+0x2b8]=%d\n", \
      $__l, $r1, *(int *)($r1 + 0x2b4), *(int *)($r1 + 0x2b8)
  end
  if $__l >= 4096
    detach
    quit
  end
  continue
end
printf "limit probe armed at 0x081c08b4\n"
continue
