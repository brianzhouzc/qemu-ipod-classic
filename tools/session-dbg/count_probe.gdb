# DIAGNOSTIC (not a fix): give the codec a non-zero sample count and see if music
# comes out.
#
# Measured: inside the frame pull's loop the count handed to the codec math is
#     r3 = min([obj+0x44], [reader+8])
# and both are 0 live (obj=[reader+4]=0x0ba90c50, reader=0x093cbbd4), so
# 0x081e9e70 is invoked asking for ZERO samples -- it "decodes a frame" that
# produces nothing, which is why the mixer's source ring stays silent.
#
# This breakpoint sits just before the loop (0x081e9428) and writes a plausible
# frame's worth into both fields. If audio appears, the ONLY thing missing is
# whoever should be setting that count, and the search narrows to that writer.
# If audio still does not appear, there is another layer below and this rules the
# count out as the sole cause. Either answer is worth one run.
set pagination off
set confirm off
set $__c = 0
target remote :1234
break *0x081e9428
commands
  silent
  set $__c = $__c + 1
  set $obj = *(unsigned int *)($r4 + 4)
  if $__c <= 4
    printf "COUNT #%d before: [obj+0x44]=%d [reader+8]=%d prod=%d cons=%d\n", \
       $__c, *(int *)($obj + 0x44), *(int *)($r4 + 8), \
       *(int *)($obj + 0x3c), *(int *)($obj + 0x40)
  end
  set *(int *)($obj + 0x44) = 1024
  set *(int *)($r4 + 8) = 1024
  continue
end
printf "count probe armed at 0x081e9428 (forcing 1024 samples)\n"
continue
