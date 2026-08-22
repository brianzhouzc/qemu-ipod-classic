set pagination off
set confirm off
target remote :1234
set $cur = *(unsigned int *)0x2200ac64
printf "SNAP task=0x%02x I2S_TXCON=%08x I2S_STATUS=%08x DMAch1_SRC=%08x CTRL=%08x CONF=%08x SM1_POS=%08x SM1_RUN=%08x mb52_w2=%08x mb52_w6=%08x ex6=%08x\n", *(unsigned char *)($cur+32), *(unsigned int *)0x3ca00004, *(unsigned int *)0x3ca0003c, *(unsigned int *)0x3820010c, *(unsigned int *)0x38200118, *(unsigned int *)0x38200110, *(unsigned int *)0x38500a48, *(unsigned int *)0x38500400, *(unsigned int *)0x08978284, *(unsigned int *)0x08978298, *(unsigned int *)0x0897fb34
detach
quit
