# Assault #14: dump the descriptor the stuck memcpy is driven by.
#
# Chain (traced this session): render 0x0808170c -> mixer 0x0809a74c ->
# 0x08016f14 -> 0x08016f68 -> copy 0x08016e44 -> gate closed -> 0x08016ecc ->
# bl 0x08085ea0. That callee is NOT a wait, it is a memcpy:
#     r3 = [r1+16] >> 2      (word count)
#     r1 = [r1+12] + r2      (destination)
#     loop: [r1++] = [r0++]
# It is entered once and never returns (memcpyret 0x08085ec4 hits=0), so the
# whole native render chain is stuck inside it. We need the actual count and
# destination to say why.
set pagination off
set confirm off
target remote :1234
break *0x08085ea0
commands
  silent
  printf "MEMCPY src=0x%08x descr=0x%08x off=0x%08x\n", $r0, $r1, $r2
  printf "   [descr+12] dstbase = 0x%08x\n", *(unsigned int *)($r1 + 12)
  printf "   [descr+16] bytes   = 0x%08x (%u)\n", *(unsigned int *)($r1 + 16), *(unsigned int *)($r1 + 16)
  printf "   [descr+0]  type    = 0x%02x\n", *(unsigned char *)($r1)
  printf "   [descr+4]  rdpos   = 0x%08x\n", *(unsigned int *)($r1 + 4)
  printf "   [descr+20] flag    = 0x%02x\n", *(unsigned char *)($r1 + 20)
  printf "   => dest = 0x%08x, words = %u\n", (*(unsigned int *)($r1 + 12)) + $r2, (*(unsigned int *)($r1 + 16)) >> 2
  detach
  quit
end
printf "memcpy probe armed at 0x08085ea0\n"
continue
