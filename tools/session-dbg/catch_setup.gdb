set pagination off
set confirm off
set height 0
target remote :1234
# codec-setup entry (IRAM 0x22006574): binds a codec descriptor to an audio state
break *0x22006574
commands 1
  silent
  printf "CODEC_SETUP state=0x%08x desc=0x%08x type=0x%02x\n", $r0, $r1, *(unsigned char*)$r1
  continue
end
# gate SET site (0x220065ac: strb r7,[r4,#71]=1)
break *0x220065ac
commands 2
  silent
  printf "GATE_SET state=0x%08x\n", $r4
  continue
end
# engine gate READ (0x0808171c: ldrb r0,[state+0x47]) -- what the engine sees
break *0x0808171c
commands 3
  silent
  printf "ENGINE_GATE_READ state=0x%08x gate=0x%02x type=0x%02x\n", $r0, *(unsigned char*)($r0+0x47), *(unsigned char*)($r0+0x58)
  continue
end
continue
