# What increment is driving the playback position negative?
#
# Measured: the production window is built from [obj+0x2b4] (position) and
# [obj+0x2b8] (limit), and the position reads 0 on every sample while the limit
# reads 4. With the position pinned at 0 the window never moves, the queue depth
# stays at 1, and framepull's loop runs the codec exactly once. That is the whole
# reason aacmath == 1.
#
# The position is not simply unwritten -- it is written repeatedly, and the
# disassembly shows why it stays 0:
#
#   81cc404: adds r2, r1, r2        accumulate
#   81cc408: str  r2, [r0,#0x2b4]
#   81cc40c: bpl  0x81cc424         positive: keep it
#   81cc410: mov  r1, #0
#   81cc414: str  r1, [r0,#0x2b4]   NEGATIVE: reset to zero
#
# So the increment r1 is driving it negative. This reads r1 and r2 at the add, so
# the increment and the running total are both visible. If the increment is a
# large negative number it is coming from a subtraction against something this
# model reports wrongly -- the SM1 playback position is the obvious suspect,
# having already produced a bug of exactly this family.
set pagination off
set confirm off
set $__p = 0
target remote :1234
break *0x081cc404
commands
  silent
  set $__p = $__p + 1
  if $__p <= 12
    printf "posinc#%-2d  obj=0x%08x  increment r1=%d (0x%08x)  running r2=%d  -> sum=%d\n", \
      $__p, $r0, $r1, $r1, $r2, $r1 + $r2
  end
  if $__p >= 12
    detach
    quit
  end
  continue
end
printf "position-increment probe armed at 0x081cc404\n"
continue
