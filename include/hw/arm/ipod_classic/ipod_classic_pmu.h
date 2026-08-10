/*
 * NXP PCF50635 power management unit of the iPod classic 6G.
 *
 * Register numbers from Rockbox firmware/export/pcf5063x.h; the three
 * undocumented ones from firmware/target/arm/s5l8702/ipod6g/pmu-target.h,
 * where they are introduced as "undocummented PMU registers".
 *
 * The I2C address is 0xe6 as both drivers write it, i.e. the 8-bit form with
 * the read/write bit already in place. Rockbox says so in pmu-6g.c:
 * "i2c_read(0, 0xe6, ...)". The 7-bit address QEMU's i2c core wants is 0x73.
 */

#ifndef HW_ARM_IPOD_CLASSIC_PMU_H
#define HW_ARM_IPOD_CLASSIC_PMU_H

#include "qemu/osdep.h"
#include "hw/i2c/i2c.h"
#include "qom/object.h"

#define TYPE_IPOD_CLASSIC_PMU "pcf50635"
OBJECT_DECLARE_SIMPLE_TYPE(IPodClassicPMUState, IPOD_CLASSIC_PMU)

#define S5L8702_PMU_I2C_ADDR   0x73   /* 0xe6 >> 1 */
#define S5L8702_PMU_NUM_REGS   0x100

#define PCF5063X_REG_VERSION   0x00
#define PCF5063X_REG_VARIANT   0x01
#define PCF5063X_REG_INT1      0x02
#define PCF5063X_REG_INT2      0x03
#define PCF5063X_REG_INT3      0x04
#define PCF5063X_REG_INT4      0x05
#define PCF5063X_REG_INT5      0x06
#define PCF5063X_REG_INT1M     0x07
#define PCF5063X_REG_INT2M     0x08
#define PCF5063X_REG_INT3M     0x09
#define PCF5063X_REG_INT4M     0x0a
#define PCF5063X_REG_INT5M     0x0b
#define PCF5063X_REG_OOCSHDWN  0x0c
#define PCF5063X_REG_OOCSTAT   0x12
#define PCF5063X_REG_GPIOCTL   0x13
#define PCF5063X_REG_DCDCSTAT  0x41
#define PCF5063X_REG_LDOSTAT   0x42
#define PCF5063X_REG_MBCS1     0x4b
#define PCF5063X_REG_MBCS2     0x4c
#define PCF5063X_REG_MBCS3     0x4d
#define PCF5063X_REG_ADCC3     0x52
#define PCF5063X_REG_ADCC2     0x53
#define PCF5063X_REG_ADCC1     0x54
#define PCF5063X_REG_ADCS1     0x55
#define PCF5063X_REG_ADCS2     0x56
#define PCF5063X_REG_ADCS3     0x57
#define PCF5063X_REG_RTCSC     0x59
#define PCF5063X_REG_RTCMT     0x5e
#define PCF50635_REG_INT6      0x85
#define PCF50635_REG_INT6M     0x86
#define PCF50635_REG_GPIOSTAT  0x87

#define PCF5063X_ADCC1_ADCSTART   0x01
#define PCF5063X_ADCS3_ADCRDY     0x80

/*
 * GPIOSTAT bit 1, PCF50635_GPIOSTAT_GPIO2 in Rockbox's pmu-target.h:43. It is
 * the hold switch, inverted: pmu-6g.c:244 reads it as
 * "holdswitch = !(GPIOSTAT & GPIO2)", so SET means hold is OFF. Apple's
 * keycode dispatcher reaches the same conclusion from the other direction --
 * see the long note at the reset function. Clear it and no button on the
 * click wheel does anything at all.
 */
#define PCF50635_GPIOSTAT_HOLD_OFF  0x02

/*
 * ASSUMED. 844 of 1023 is 3900 mV through Rockbox's own conversion in
 * pmu-6g.c (raw * 2000 / 1023 + 2250). Nothing in our sources says what a real
 * cell reads; this is chosen to be an ordinary value rather than an
 * interesting one.
 */
#define S5L8702_PMU_ADC_BATTERY_RAW  844

struct IPodClassicPMUState {
    I2CSlave parent_obj;

    uint8_t reg[S5L8702_PMU_NUM_REGS];
    uint8_t addr;         /* the register pointer, which survives a STOP */
    bool addr_pending;    /* next byte written is the register number */
};

#endif
