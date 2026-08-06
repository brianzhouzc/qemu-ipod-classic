/*
 * PP502x interrupt controller. See hw/arm/ipod_video/ipod_video_intc.c.
 */

#ifndef HW_ARM_IPOD_VIDEO_INTC_H
#define HW_ARM_IPOD_VIDEO_INTC_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_IPOD_VIDEO_INTC "pp502x.intc"
OBJECT_DECLARE_SIMPLE_TYPE(IPodVideoINTCState, IPOD_VIDEO_INTC)

/* Low bank at 0x60004000, high bank at 0x60004100. */
#define INTC_NUM_BANKS  2

typedef struct {
    uint32_t raw;           /* lines asserted by devices */
    uint32_t forced;        /* lines asserted by software through FORCED_SET */
    uint32_t cpu_enabled;
    uint32_t cop_enabled;
} IPodVideoINTCBank;

struct IPodVideoINTCState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;           /* to the CPU */
    IPodVideoINTCBank bank[INTC_NUM_BANKS];
};

#endif
