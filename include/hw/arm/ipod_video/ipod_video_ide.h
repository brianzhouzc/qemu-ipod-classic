/*
 * The PP502x IDE controller.
 *
 * A standard ATA task file with the registers four bytes apart, plus a block
 * of PortalPlayer-specific configuration, timing and DMA registers below it.
 * Addresses come from Rockbox's firmware/export/pp5020.h and
 * firmware/target/arm/pp/ata-target.h; the DMA sequence is from
 * firmware/target/arm/pp/ata-pp5020.c.
 */

#ifndef HW_ARM_IPOD_VIDEO_IDE_H
#define HW_ARM_IPOD_VIDEO_IDE_H

#include "hw/sysbus.h"
#include "hw/ide/internal.h"
#include "qom/object.h"

#define TYPE_PP502X_IDE "pp502x-ide"
OBJECT_DECLARE_SIMPLE_TYPE(PP502xIDEState, PP502X_IDE)

/* The whole window the controller answers to, task file included. */
#define PP502X_IDE_REGION_SIZE  0x1000

struct PP502xIDEState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    IDEBus bus;
    IDEDMA dma;
    MemoryRegion iomem;
    qemu_irq irq;

    /*
     * Every register in the window that is not the task file reads back what
     * was written. Only a handful have behaviour; the rest are timings the
     * firmware programs and never checks.
     */
    uint32_t regs[PP502X_IDE_REGION_SIZE / 4];

    /*
     * IDE0_CFG bit 3. This is a latch rather than a mirror of the drive's
     * interrupt line: during DMA the firmware never reads ATA status (doing so
     * "causes failures and hangs", per ata-pp5020.c), so this is the only way
     * it learns the transfer finished. It clears by writing a one back.
     */
    bool intrq;

    /* Set between the ATA command and the write that starts the transfer. */
    BlockCompletionFunc *dma_cb;
    uint32_t dma_cur;
    uint32_t dma_left;
};

void pp502x_ide_init_drive(DeviceState *dev, DriveInfo *hd);

#endif /* HW_ARM_IPOD_VIDEO_IDE_H */
