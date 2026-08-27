# Why does the decoder never ask for a second frame?
#
# 0x081e9bc0 is not a spin loop, it is a getter, and the disassembly says exactly
# when it gives up:
#
#   81e9bc0: bl 0x81c0fd4        credit; returns 4 here, so NOT the bail
#   81e9bd8: ldr r1,[[r4]+0x48]  vtable slot 0x48
#   81e9be4: blx r1              -> r5 = next buffer
#   81e9bfc: ldrb r0,[r4,#0x30]
#   81e9c00: cmp r0,#3
#   81e9c04: beq 0x81e9c34       -> RETURN NULL
#   81e9c08: cmp r5,#0
#   81e9c0c: beq 0x81e9bf0       -> buffer NULL: pump 0x81e96d0 and retry
#   81e9c14: bl 0x83614bc ; ldrb r0,[r0,#0x74] ; cmp r0,#5 ; bcs retry
#
# So there are only three ways out, and they are distinguishable: state byte
# [r4+0x30] == 3, a NULL buffer, or that [.+0x74] >= 5. 0x081e9bfc is a branch
# target, so the registers are reliable there.
set pagination off
set confirm off
set $__n = 0
target remote :1234
break *0x081e9bfc
commands
  silent
  set $__n = $__n + 1
  if $__n <= 8
    printf "slot#%d  r4=0x%08x  r5(buffer)=0x%08x  state[r4+0x30]=%d\n", \
      $__n, $r4, $r5, *(unsigned char *)($r4 + 0x30)
    if $r5 != 0
      printf "    [r5+0x74] via 0x83614bc is the other gate; r5 fields: %08x %08x %08x\n", \
        *(unsigned int *)($r5), *(unsigned int *)($r5+4), *(unsigned int *)($r5+8)
    end
  end
  if $__n >= 8
    detach
    quit
  end
  continue
end
printf "slot probe armed at 0x081e9bfc\n"
continue
