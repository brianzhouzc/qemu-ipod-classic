# Walk the audio DMA's LLI chain and look at what it is actually feeding the I2S.
#
# Measured: ch1 src=0x22009c5c dst=0x3ca00010 lli=0x22010020 ctl=0x84249800.
# ctl decodes to dest-fixed, src-increment, 16-bit width, 2048 transfers (4KB/node),
# so the DMA wants 16-bit PCM out of IRAM0 -- while the render's copy was measured
# writing into IRAM1 at 0x22026400. If every node in the chain is zeros, the silence
# is a buffer the firmware never filled, and the chain tells us which one.
#
# A PL080 LLI node is 4 words: SrcAddr, DstAddr, NextLLI, Control.
set pagination off
set confirm off
set $__n = 0
target remote :1234
break *0x08016edc
commands
  silent
  set $__n = $__n + 1
  if $__n == 2
    printf "=== ch1 live: src=0x%08x lli=0x%08x ctl=0x%08x ===\n", \
      *(unsigned int *)0x38200120, *(unsigned int *)0x38200128, \
      *(unsigned int *)0x3820012c
    set $lli = *(unsigned int *)0x38200128
    set $k = 0
    while $k < 6 && $lli >= 0x22000000 && $lli < 0x22040000
      set $ns = *(unsigned int *)($lli)
      set $nd = *(unsigned int *)($lli+4)
      set $nn = *(unsigned int *)($lli+8)
      set $nc = *(unsigned int *)($lli+12)
      printf "LLI[%d]@0x%08x src=0x%08x dst=0x%08x next=0x%08x ctl=0x%08x\n", \
        $k, $lli, $ns, $nd, $nn, $nc
      if $ns >= 0x22000000 && $ns < 0x22040000
        set $nz = 0
        set $j = 0
        while $j < 512
          if *(unsigned int *)($ns + $j*4) != 0
            set $nz = $nz + 1
          end
          set $j = $j + 1
        end
        printf "    nonzero words in 2KB: %d   head: %08x %08x %08x %08x\n", $nz, \
          *(unsigned int *)($ns), *(unsigned int *)($ns+4), \
          *(unsigned int *)($ns+8), *(unsigned int *)($ns+12)
      end
      set $lli = $nn
      set $k = $k + 1
    end
    # and the live source pointer's own neighbourhood
    set $cs = *(unsigned int *)0x38200120
    printf "live src 0x%08x: %08x %08x %08x %08x\n", $cs, \
      *(unsigned int *)($cs), *(unsigned int *)($cs+4), \
      *(unsigned int *)($cs+8), *(unsigned int *)($cs+12)
    printf "render dst 0x22026400: %08x %08x %08x %08x\n", \
      *(unsigned int *)0x22026400, *(unsigned int *)0x22026404, \
      *(unsigned int *)0x22026408, *(unsigned int *)0x2202640c
    detach
    quit
  end
  continue
end
printf "lli probe armed\n"
continue
