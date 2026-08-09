/*
 * NXP PCF50605 power management unit, the chip on the iPod 5G's I2C bus at
 * 7-bit address 0x08. See hw/arm/ipod_video/ipod_video_pcf50605.c.
 */

#ifndef HW_ARM_IPOD_VIDEO_PCF50605_H
#define HW_ARM_IPOD_VIDEO_PCF50605_H

#include "hw/i2c/i2c.h"
#include "qom/object.h"

#define TYPE_PCF50605 "pcf50605"
OBJECT_DECLARE_SIMPLE_TYPE(PCF50605State, PCF50605)

/* The register map runs 0x00..0x3c; round up to a power of two. */
#define PCF50605_NUM_REGS   0x40

/* Rockbox's pcf50605.c: i2c_readbyte(0x8, ...), pp_i2c_send(0x8, ...). */
#define PCF50605_I2C_ADDR   0x08

struct PCF50605State {
    /*< private >*/
    I2CSlave parent_obj;

    /*< public >*/
    uint8_t regs[PCF50605_NUM_REGS];

    /*
     * The register pointer. It survives the end of a transaction: both drivers
     * set it with a one-byte write and then read the data back in separate
     * transactions (Rockbox's i2c_readbytes(), Apple's 0x283540).
     */
    uint8_t ptr;
    /* Set by a start-of-write; the first byte of a write is the pointer. */
    bool expect_ptr;

    /*
     * Seconds between the host clock and what the RTC registers report, so a
     * guest that sets the clock sees its own value back. Zero means the RTC
     * agrees with whatever QEMU's -rtc option says the time is.
     */
    int64_t rtc_offset;
};

#endif /* HW_ARM_IPOD_VIDEO_PCF50605_H */
