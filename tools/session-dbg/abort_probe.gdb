# Who reads past the end of IRAM1?
#
# QEMU's own -d int log says the run takes exactly ONE Data Abort, with
# DFSR 0x8 (external abort, non-linefetch) and DFAR 0x22040216 -- 0x216 bytes
# past the end of IRAM1, which ends at 0x22040000. That single fault is what
# turns into CPU_exception_handler and the software reset that cuts playback
# short, so it is the one thing standing between a working library and music.
#
# A memw watch on 0x22040000-0x22040800 saw ZERO stores, so it is a LOAD. This
# catches it at the ARM data-abort vector (0x10, in the IRAM0 alias at 0), where
# lr_abt - 8 is the address of the instruction that faulted, and dumps the
# registers so the base and index it was computed from are visible.
set pagination off
set confirm off
set $__a = 0
target remote :1234
break *0x10
commands
  silent
  set $__a = $__a + 1
  printf "=== DATA ABORT #%d ===\n", $__a
  printf "faulting insn at 0x%08x  (lr_abt=0x%08x)\n", $lr - 8, $lr
  printf "  r0=%08x r1=%08x r2=%08x r3=%08x\n", $r0, $r1, $r2, $r3
  printf "  r4=%08x r5=%08x r6=%08x r7=%08x\n", $r4, $r5, $r6, $r7
  printf "  r8=%08x r9=%08x r10=%08x r11=%08x\n", $r8, $r9, $r10, $r11
  printf "  r12=%08x sp=%08x cpsr=%08x\n", $r12, $sp, $cpsr
  x/3i $lr - 8
  # The copy is descriptor-driven: r4 held 0x0bb12d54 and r5 0x0888ba74 when it
  # faulted. Dump both, because the destination base and the length the loop was
  # given are in there, and the base plus that length is what has to be compared
  # against the top of IRAM1 (0x22040000) to see who got the ring size wrong.
  printf "  descriptor at r4=0x%08x:\n", $r4
  set $i = 0
  while $i < 16
    printf "    +0x%02x = %08x\n", $i*4, *(unsigned int *)($r4 + $i*4)
    set $i = $i + 1
  end
  printf "  object at r5=0x%08x:\n", $r5
  set $i = 0
  while $i < 12
    printf "    +0x%02x = %08x\n", $i*4, *(unsigned int *)($r5 + $i*4)
    set $i = $i + 1
  end
  if $__a >= 2
    detach
    quit
  end
  continue
end
printf "data-abort probe armed at the vector\n"
continue
