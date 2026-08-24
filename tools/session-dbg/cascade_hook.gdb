# Assault #13 -- DUAL PC-hook: make the motor believe it received a render
# command, AND let its copy actually copy. Tests whether the native render can
# be driven continuously (the cascade question left open by copy32=2).
#
# Hook 1 -- 0x22006824 (motor recv return site, reached every 3.00s, measured):
#     22006814: push {r4,lr} / ldr r1,=0xbb8 / ldr r0,=0x220106d0
#     22006820: bl 0x22003868      <- KS recv(mailbox 0x52, timeout 3000ms)
#     22006824: cmp r0, #0         <- WE STOP HERE and force r0 = 0
#     22006828: mvnne r0, #0       (skipped once r0==0)
#   Forcing r0=0 makes the wrapper return SUCCESS, i.e. "a render command
#   arrived", instead of -1 (timeout -> codec teardown).
#
# Hook 2 -- 0x08016e88 (copy gate check): force [r7+5]=1 so the copy proceeds.
#
# Together: motor renders repeatedly, each render copies real PCM. If the
# native pipeline can be driven at all, copy32 climbs and Now Playing advances.
# No firmware patch; purely external and reversible.
set pagination off
set confirm off
set $__rc = 0
set $__cg = 0
target remote :1234

# --- Hook 1: motor recv returns success ---
break *0x22006824
commands
  silent
  set $r0 = 0
  set $__rc = $__rc + 1
  if $__rc <= 6
    printf "[cascade] recv-success #%d forced (t=%d)\n", $__rc, $__rc
  end
  continue
end

# --- Hook 2: copy gate forced open ---
break *0x08016e88
commands
  silent
  set {unsigned char}($r7 + 5) = 1
  set {unsigned char}($r4) = 0
  set $__cg = $__cg + 1
  if $__cg <= 6
    printf "[cascade] copy-gate #%d forced r7=0x%x\n", $__cg, $r7
  end
  continue
end

printf "cascade hooks armed (recv 0x22006824, gate 0x08016e88)\n"
continue
