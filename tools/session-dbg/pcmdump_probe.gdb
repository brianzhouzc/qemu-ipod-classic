# Turn ON the firmware's OWN PCM dump and read back what it is handed.
#
# 0x0816bf48 is the retailOS PCM output path. Inside it, at 0x0816c05c, it checks a
# flag struct at 0x08948f24: byte 0 = "dumping enabled", +4 = remaining byte budget.
# When enabled it opens "MeCCA_PCM_Output.wav" and writes r6 for r4*4 bytes.
#
# Rather than trust the guest's filesystem write, we breakpoint the function entry
# and read the arguments ourselves: that tells us directly whether the samples the
# output path receives are music or zeros -- which settles whether the decoder is
# producing anything at all, without any instrumentation of our own in the datapath.
set pagination off
set confirm off
set $__n = 0
target remote :1234

# Arm the firmware's own dump flag as soon as we are attached.
set {unsigned char}0x08948f24 = 1
set {unsigned int}0x08948f28 = 0x02000000

break *0x0816bf48
commands
  silent
  set $__n = $__n + 1
  printf "PCMOUT #%d  r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x\n", $__n, $r0, $r1, $r2, $r3
  if $__n <= 6
    printf "   flag=%d budget=0x%x\n", *(unsigned char *)0x08948f24, *(unsigned int *)0x08948f28
  end
  if $__n >= 12
    detach
    quit
  end
  continue
end
printf "pcm-dump probe armed at 0x0816bf48 (firmware dump flag forced on)\n"
continue
