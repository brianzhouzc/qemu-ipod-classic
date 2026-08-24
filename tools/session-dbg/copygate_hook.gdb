# Assault #13 -- TCG PC-hook (via gdb BP_GDB breakpoint) that forces the copy
# gate open at the exact instruction, winning the race the fixed-address poke
# lost (guest rewrites [r7+5]=0 ~0us before the read; wiki §11.24 follow-up).
#
# At 0x08016e88 the copy does `ldrb r1,[r7,#5]; cmp r1,#1; bne skip`. We stop
# BEFORE the ldrb, write 1 into the guest byte [r7+5], and continue: the ldrb
# then reads 1, the gate passes, the copy runs (copy32>0). Fires every time the
# copy is reached, so we can see whether forcing it CASCADES (self-sustaining
# render) or is a single transient step (wiki §11.18 symmetry question).
#
# No firmware patch, no core edit: pure external hook. Reversible (don't run).
set pagination off
set confirm off
target remote :1234
# gate-check PC (before the ldrb that reads [r7+5])
break *0x08016e88
commands
  silent
  # r7 holds the gate descriptor; +5 is the "frame ready" byte
  set {unsigned char}($r7 + 5) = 1
  # also keep the [r4]==0 path byte clear in case the guest set it
  set {unsigned char}($r4) = 0
  set $__cg = $__cg + 1
  continue
end
set $__cg = 0
printf "copygate hook armed at 0x08016e88\n"
continue
