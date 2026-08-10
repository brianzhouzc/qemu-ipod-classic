/*
 * Control case for the S5L8702 panel model.
 *
 * Drives the LCD block at 0x38300000 with exactly the sequence Apple's retailOS
 * emits -- LCD_CON = 0x80000c21 (8-bit command mode), 0x2A with {x, xe},
 * 0x2B with {y, ye}, 0x2C, LCD_CON = 0x80100db1 (16-bit frame mode) -- and then
 * pushes 320*240 RGB565 words through LCD_WDATA. If the panel in
 * ipod_classic_lcd.c is right, a screendump comes back as a colour ramp; if the
 * window arithmetic or the pixel format is wrong, it comes back wrong in an
 * obvious way. Nothing else in this tree can tell those two apart.
 *
 *   arm-none-eabi-as -o lcdtest.o lcdtest.S
 *   arm-none-eabi-objcopy -O binary lcdtest.o lcdtest.bin
 *   qemu-system-arm -M iPod-Classic -kernel lcdtest.bin ...
 */
        .arm
        .text
        .global _start
_start:
        ldr     r0, =0x38300000         @ LCD_BASE

        ldr     r1, =0x80000c21         @ LCD_MODE_P8 | run: command mode
        str     r1, [r0, #0x00]

        mov     r1, #0x2a               @ column address set
        str     r1, [r0, #0x04]
        mov     r1, #0
        str     r1, [r0, #0x40]         @ x  = 0x0000
        str     r1, [r0, #0x40]
        mov     r1, #1
        str     r1, [r0, #0x40]         @ xe = 0x013f = 319
        mov     r1, #0x3f
        str     r1, [r0, #0x40]

        mov     r1, #0x2b               @ page/row address set
        str     r1, [r0, #0x04]
        mov     r1, #0
        str     r1, [r0, #0x40]         @ y  = 0x0000
        str     r1, [r0, #0x40]
        str     r1, [r0, #0x40]
        mov     r1, #0xef               @ ye = 0x00ef = 239
        str     r1, [r0, #0x40]

        mov     r1, #0x2c               @ memory write
        str     r1, [r0, #0x04]

        ldr     r1, =0x80100db1         @ LCD_MODE_P16 | run: frame mode
        str     r1, [r0, #0x00]

        @ 240 rows of 320 pixels. Red ramps with x, green with y, so a correct
        @ panel shows a two-axis gradient and a transposed one shows it rotated.
        mov     r4, #0                  @ y
row:
        mov     r3, #0                  @ x
col:
        mov     r2, r3, lsr #3          @ x >> 3  -> 0..39, into red (5 bits)
        and     r2, r2, #0x1f
        mov     r2, r2, lsl #11
        mov     r5, r4, lsr #2          @ y >> 2  -> 0..59, into green (6 bits)
        and     r5, r5, #0x3f
        orr     r2, r2, r5, lsl #5
        orr     r2, r2, #0x08           @ a little blue everywhere, so no pixel
                                        @ of the picture is exactly black
        str     r2, [r0, #0x40]
        add     r3, r3, #1
        cmp     r3, #320
        bne     col
        add     r4, r4, #1
        cmp     r4, #240
        bne     row

        @ Start the frame engine the way the retailOS does, to prove that
        @ writing +0x74/+0x70 does not disturb what is already in the panel.
        ldr     r1, =0x014000f0
        str     r1, [r0, #0x74]
        mov     r1, #1
        str     r1, [r0, #0x70]

spin:   b       spin
