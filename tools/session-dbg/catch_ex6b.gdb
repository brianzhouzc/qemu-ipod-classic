set pagination off
set confirm off
set height 0
target remote :1234
# exchange 6 slot (RTXC exchange/sema table 0x0897fb1c stride 4, idx 6)
watch *(unsigned int*)0x0897fb34
commands 1
  silent
  printf "EX6  val=0x%08x  pc=0x%08x  lr=0x%08x\n", *(unsigned int*)0x0897fb34, $pc, $lr
  continue
end
# mbox 0x52 sequence counter (w6 @ +0x18 of mbox 0x0897827c)
watch *(unsigned int*)0x08978288
commands 2
  silent
  printf "M52w6 val=0x%08x  pc=0x%08x  lr=0x%08x\n", *(unsigned int*)0x08978288, $pc, $lr
  continue
end
continue
