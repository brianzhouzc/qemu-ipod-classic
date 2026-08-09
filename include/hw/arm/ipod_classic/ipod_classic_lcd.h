/*
 * S5L8702 LCD interface, 0x38300000 (LCD_BASE).
 *
 * This is the MPU/8080-style command port that talks to the panel controller.
 * It is not a scanout engine: there is no framebuffer address here and no
 * video timing. Software hands it commands and pixel words and the panel keeps
 * its own image memory. The separate block at 0x39200000 (LCDBASE in Rockbox,
 * "CLCD CONTROLLER") is the one with frame buffer start/end addresses, and the
 * retailOS does not appear to use it on this machine.
 *
 * Register names and offsets from Rockbox, firmware/export/s5l87xx.h, the
 * "CONFIG_CPU==S5L8702 || CONFIG_CPU==S5L8720" arm of the #if at line 1473.
 * Note that the same header defines LCD_BASE as 0x3C100000 for the S5L8700 --
 * a different chip, and not to be confused with the unnamed block this machine
 * maps at that address.
 *
 * The status bits are the part that matters, because every one of them is a
 * spin with no timeout on both sides. Rockbox and Apple's retailOS agree on
 * all three, which is what makes them safe to build on:
 *
 *   bit 0  read data in LCD_DBUFF is valid
 *          rockbox lcd-s5l8702.c:241  "while (!(LCD_STATUS & 1));"
 *   bit 1  interface idle, a control write may proceed
 *          rockbox lcd-s5l8702.c:207  "while (!(LCD_STATUS & 0x2));"
 *          retailOS 0x080b6f98, 0x080b6ffc  same loop, then LCD_CON = x
 *   bit 4  write FIFO busy, do not push another word
 *          rockbox lcd-s5l8702.c:215  "while (LCD_STATUS & 0x10);"
 *          retailOS 0x080a90b4         same loop
 *
 * s5l87xx.h records the reset value as 0x0106, i.e. bits 1, 2 and 8 set and
 * bit 4 clear -- idle and not busy, which is consistent with the above. What
 * bits 2 and 8 mean is UNDETERMINED; no driver we have tests them.
 */

#ifndef HW_ARM_IPOD_CLASSIC_LCD_H
#define HW_ARM_IPOD_CLASSIC_LCD_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_IPOD_CLASSIC_LCD "s5l8702.lcd"
OBJECT_DECLARE_SIMPLE_TYPE(IPodClassicLCDState, IPOD_CLASSIC_LCD)

/*
 * Rockbox names registers up to LCD_WDATA at 0x40, "0x40...0x5C FIXME", but
 * the block is wider than Rockbox knows: the retailOS writes 0x3830007C and
 * 0x38300088 during its very first LCD setup, before it touches anything
 * Rockbox names. Those two are UNDETERMINED -- see ipod_classic_lcd.c -- but
 * the window has to cover them or they fall through to the machine's io floor.
 */
#define S5L8702_LCD_MEM_SIZE    0x100

#define S5L8702_LCD_CON         0x00
#define S5L8702_LCD_WCMD        0x04
#define S5L8702_LCD_RCMD        0x0C
#define S5L8702_LCD_RDATA       0x10
#define S5L8702_LCD_DBUFF       0x14
#define S5L8702_LCD_INTCON      0x18
#define S5L8702_LCD_STATUS      0x1C
#define S5L8702_LCD_PHTIME      0x20
#define S5L8702_LCD_RST_TIME    0x24
#define S5L8702_LCD_DRV_RST     0x28
#define S5L8702_LCD_WDATA       0x40   /* 0x40..0x5C all write pixel data */
#define S5L8702_LCD_WDATA_LAST  0x5C

#define S5L8702_LCD_STATUS_RDATA   (1u << 0)
#define S5L8702_LCD_STATUS_IDLE    (1u << 1)
#define S5L8702_LCD_STATUS_BUSY    (1u << 4)
/* Bits 2 and 8 of the 0x0106 reset value; meaning UNDETERMINED. */
#define S5L8702_LCD_STATUS_RESET   0x0106

struct IPodClassicLCDState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    uint32_t con;
    uint32_t intcon;
    uint32_t phtime;
    uint32_t rst_time;
    uint32_t drv_rst;

    /* Result of the last read cycle, handed back through LCD_DBUFF. */
    uint32_t dbuff;
    bool dbuff_valid;
    /* Command byte of the read cycle in progress, from LCD_RCMD. */
    uint32_t rcmd;
};

#endif /* HW_ARM_IPOD_CLASSIC_LCD_H */
