/*
 * S5L8702 GPIO, 0x3CF00000 (GPIO_BASE).
 *
 * Sixteen groups of eight pins, 0x20 bytes of registers each. Layout from
 * Rockbox, firmware/export/s5l87xx.h:1370-1384 (the GPIO_OFFSET_BITS==5 arm,
 * which is the S5L8702):
 *
 *   +0x00 PCON(i)   pin configuration, four bits per pin
 *   +0x04 PDAT(i)   pin data
 *   +0x08 PUNA(i)
 *   +0x0C PUNB(i)
 *   +0x10 PUNC(i)
 *
 * Pin n lives in group n>>3 at bit n&7. Apple's accessor is 0x08325584:
 *
 *   r2 = [GPIO_BASE + ((pin >> 3) << 5) + 4]   ; PDAT(pin >> 3)
 *   *out = (r2 >> (pin & 7)) & 1
 *
 * WHY THIS DEVICE EXISTS, and it is one bit:
 *
 * 0x080aeca4 is gpio_get_pin(86) and nothing else. 0x08328720 -- the body of
 * the RTXC task that owns storage arrival -- calls it as the last of four
 * guards and posts message type 0 ("mount") to queue 4 when it is 1, or type 1
 * ("no media") when it is 0:
 *
 *   bl 0x08294d7c ; cmp r0,#0 ; bne no_media     (PMU register 0x4b bit 2)
 *   bl 0x08295508 ; cmp r0,#0 ; bne no_media     (PMU 0x12/0x4b)
 *   bl 0x080a1474 ; cmp r0,#0 ; bne no_media     ([0x089120a1] != 100)
 *   bl 0x080aeca4 ; cmp r0,#0 ; movne r1,#0      <- gpio_get_pin(86)
 *   mov r0,#4 ; bl 0x080b2ce8                    ; post {r1} to queue 4
 *
 * MEASURED with a disk attached: the first three guards return 0 and the
 * fourth returns 0 as well, because GPIO was create_unimplemented_device() and
 * read as zero. So the mount message was never posted, 0x08122a04 never set the
 * volume-ready byte at [obj+0x28], and 0x081228c4 -- "while (!ready) wait();"
 * -- parked the UI task for ever. That is the whole reason a boot with a disk
 * reached the RTXC idle loop and never drew.
 *
 * What pin 86 is CALLED is UNDETERMINED: Rockbox does not name it and no
 * datasheet we have covers it. What it DOES in Apple's firmware is verified
 * above, so this model drives it from the one fact it can defend -- whether a
 * drive is attached to the ATA controller. With no -drive it reads 0 and the
 * machine behaves exactly as it did before this device existed.
 *
 * Every other pin reads 0, which is also exactly what the unimplemented device
 * did. Two are worth naming because the firmware reads them:
 *
 *   pins 52,53 -> PDAT(6) bits 4,5, read by 0x0807ff94 (lcd_get_type). Zero
 *                 selects panel type 0, whose driver record is 0x08911fe0 and
 *                 whose command set is the 8-bit one -- consistent with the
 *                 LCD_CON = 0x80000c21 the guest actually writes. Leave at 0.
 *   PDAT(11)   -> read about 1200 times per 30 s by a periodic task. Leaving
 *                 it 0 is what the machine has always done.
 */

#ifndef HW_ARM_IPOD_CLASSIC_GPIO_H
#define HW_ARM_IPOD_CLASSIC_GPIO_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_IPOD_CLASSIC_GPIO "s5l8702.gpio"
OBJECT_DECLARE_SIMPLE_TYPE(IPodClassicGPIOState, IPOD_CLASSIC_GPIO)

#define S5L8702_GPIO_MEM_SIZE   0x1000
#define S5L8702_GPIO_GROUPS     16
#define S5L8702_GPIO_STRIDE     0x20

#define S5L8702_GPIO_PCON       0x00
#define S5L8702_GPIO_PDAT       0x04
#define S5L8702_GPIO_PUNA       0x08
#define S5L8702_GPIO_PUNB       0x0C
#define S5L8702_GPIO_PUNC       0x10

/* The one pin this device exists for. See the note above. */
#define S5L8702_GPIO_PIN_MEDIA  86

struct IPodClassicGPIOState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint32_t pcon[S5L8702_GPIO_GROUPS];
    uint32_t puna[S5L8702_GPIO_GROUPS];
    uint32_t punb[S5L8702_GPIO_GROUPS];
    uint32_t punc[S5L8702_GPIO_GROUPS];
    /* What the pins read as. Inputs only; see the note on writes in the .c. */
    uint32_t pdat[S5L8702_GPIO_GROUPS];

    /* Property: drives pin 86. Set by the machine from drive_get(IF_IDE). */
    bool media_present;
};

#endif /* HW_ARM_IPOD_CLASSIC_GPIO_H */
