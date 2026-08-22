set pagination off
set confirm off
set height 0
target remote :1234
# object-6 exchange slot, mbox 0x52 receiver slot, mbox 0x9 receiver slot
watch *(unsigned int *)0x0897fb34
watch *(unsigned int *)0x08978284
watch *(unsigned int *)0x08977964
set $n = 0
while $n < 120
  continue
  set $cur = *(unsigned int *)0x2200ac64
  set $tn = *(unsigned char *)($cur + 32)
  printf "[%d] pc=%08x lr=%08x task=0x%02x  ex6=%08x mb52rx=%08x mb09rx=%08x\n", $n, $pc, $lr, $tn, *(unsigned int *)0x0897fb34, *(unsigned int *)0x08978284, *(unsigned int *)0x08977964
  set $n = $n + 1
end
detach
quit
