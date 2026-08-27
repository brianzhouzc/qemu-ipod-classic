# Is the audio DMA reading from where the render writes?
#
# The render's copy demonstrably runs thousands of stores into IRAM1 (destination
# measured 0x22026400, inside 0x22020000-0x22040000) and the mixer's source buffers
# hold non-zero data -- yet the I2S capture is silent. That points at the far end:
# what the DMA is actually fetching.
#
# DMAC0 is a PL080 at 0x38200000; per channel n the registers are
#   0x100 + n*0x20 SrcAddr, +4 DestAddr, +8 LLI, +0xc Control, +0x10 Config.
# Audio is channel 1 (IIS0_TX). Break inside the copy -- which only runs during
# playback -- and read both channels plus the first words the DMA would fetch.
set pagination off
set confirm off
set $__d = 0
target remote :1234
break *0x08016edc
commands
  silent
  set $__d = $__d + 1
  if $__d <= 2
    printf "DMA ch0 src=0x%08x dst=0x%08x lli=0x%08x ctl=0x%08x cfg=0x%08x\n", \
      *(unsigned int *)0x38200100, *(unsigned int *)0x38200104, \
      *(unsigned int *)0x38200108, *(unsigned int *)0x3820010c, \
      *(unsigned int *)0x38200110
    printf "DMA ch1 src=0x%08x dst=0x%08x lli=0x%08x ctl=0x%08x cfg=0x%08x\n", \
      *(unsigned int *)0x38200120, *(unsigned int *)0x38200124, \
      *(unsigned int *)0x38200128, *(unsigned int *)0x3820012c, \
      *(unsigned int *)0x38200130
    printf "  enabled mask=0x%08x\n", *(unsigned int *)0x3820001c
    set $s1 = *(unsigned int *)0x38200120
    if $s1 > 0x08000000
      printf "  ch1 source bytes: %08x %08x %08x %08x\n", \
        *(unsigned int *)($s1), *(unsigned int *)($s1+4), \
        *(unsigned int *)($s1+8), *(unsigned int *)($s1+12)
    end
    printf "  IRAM1 render area 0x22026400: %08x %08x %08x %08x\n", \
      *(unsigned int *)0x22026400, *(unsigned int *)0x22026404, \
      *(unsigned int *)0x22026408, *(unsigned int *)0x2202640c
  end
  if $__d >= 4
    detach
    quit
  end
  continue
end
printf "dma probe armed\n"
continue
