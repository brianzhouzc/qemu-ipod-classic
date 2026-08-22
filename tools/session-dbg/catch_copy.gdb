set pagination off
set confirm off
target remote :1234
break *0x08016e60
set $n=0
while $n<4
  continue
  set $src = *(unsigned int*)($r5+12) + (( *(unsigned int*)($r4+4) - $r6 - *(unsigned int*)$r5) << 2)
  printf "COPY #%d: r5=%08x r4=%08x r7=%08x | [r5+12]buf=%08x [r5]=%08x [r4+4]=%08x [r4+16]=%08x [r4]=%02x [r7+5]=%02x => SRC=%08x count=%d\n", $n, $r5, $r4, $r7, *(unsigned int*)($r5+12), *(unsigned int*)$r5, *(unsigned int*)($r4+4), *(unsigned int*)($r4+16), *(unsigned char*)$r4, *(unsigned char*)($r7+5), $src, (*(unsigned int*)($r4+16))>>3
  printf "   SRC content: %08x %08x %08x %08x\n", *(unsigned int*)$src, *(unsigned int*)($src+4), *(unsigned int*)($src+8), *(unsigned int*)($src+12)
  set $n=$n+1
end
detach
quit
