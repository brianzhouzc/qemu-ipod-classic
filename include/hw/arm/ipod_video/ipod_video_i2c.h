/*
 * The PP502x I2C controller.
 *
 * Register map from Rockbox's firmware/target/arm/pp/i2c-pp.c (which in turn
 * comes from iPodLinux's arch/armnommu/mach-ipod/hardware.c) and from Apple's
 * own driver in apple_retailos.bin. See hw/arm/ipod_video/ipod_video_i2c.c.
 */

#ifndef HW_ARM_IPOD_VIDEO_I2C_H
#define HW_ARM_IPOD_VIDEO_I2C_H

#include "hw/sysbus.h"
#include "hw/i2c/i2c.h"
#include "qom/object.h"

#define TYPE_PP502X_I2C "pp502x-i2c"
OBJECT_DECLARE_SIMPLE_TYPE(PP502xI2CState, PP502X_I2C)

/*
 * The controller sits at the bottom of the 0x7000c000 window; the click wheel
 * has +0x100 upwards. Claim a quarter of a page so the two never overlap.
 */
#define PP502X_I2C_REGION_SIZE  0x100

/* Every register in the block is a byte, four bytes apart, up to +0x3c. */
#define PP502X_I2C_NUM_REGS     0x40

struct PP502xI2CState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion iomem;
    I2CBus *bus;
    uint8_t regs[PP502X_I2C_NUM_REGS];
};

I2CBus *pp502x_i2c_get_bus(DeviceState *dev);

#endif /* HW_ARM_IPOD_VIDEO_I2C_H */
