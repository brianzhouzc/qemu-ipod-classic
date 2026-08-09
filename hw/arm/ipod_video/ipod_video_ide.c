/*
 * The PP502x IDE controller.
 *
 * The task file is standard ATA with the registers four bytes apart, so the
 * IDE core drives the drive itself. What is PortalPlayer-specific is the block
 * below it: bus timings, IDE0_CFG, and a DMA engine.
 *
 * The DMA is not PCI bus-master, it is three registers and a flat destination
 * buffer. From ata-pp5020.c, a read is set up like this:
 *
 *     IDE0_CFG |= IDE_CFG_INTRQ;      -- clear any interrupt still pending
 *     IDE_DMA_CONTROL |= 2;
 *     IDE_DMA_LENGTH = bytes - 4;
 *     IDE_DMA_ADDR = addr;
 *     IDE_DMA_CONTROL |= 8;           -- direction: to memory
 *     IDE0_CFG |= 0x8000;
 *     <ATA command 0xc8>
 *     IDE_DMA_CONTROL |= 1;           -- and only now does it move
 *     while (!(IDE0_CFG & IDE_CFG_INTRQ)) yield();
 *
 * The order is what shapes this model: the address and length are programmed
 * before the ATA command, and the bit that starts the transfer is written
 * after it. So start_dma cannot run the transfer, it can only latch the
 * callback the IDE core hands us; the write of the go bit is what fires it.
 * This is the same split the PCI bus-master IDE uses, for the same reason.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include "sysemu/dma.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/arm/ipod_video/ipod_video_ide.h"

/* Register offsets within the window, from pp5020.h and ata-target.h. */
#define PP_IDE_TIMING_FIRST     0x000   /* four controllers, two words each */
#define PP_IDE_TIMING_LAST      0x01c
#define PP_IDE0_CFG             0x028
#define PP_IDE1_CFG             0x02c
#define PP_IDE_TASKFILE         0x1e0   /* eight registers, four bytes apart */
#define PP_IDE_TASKFILE_SIZE    0x020
#define PP_IDE_CONTROL          0x3f8   /* device control / alternate status */
#define PP_IDE_CONTROL_SIZE     0x008
#define PP_IDE_DMA_CONTROL      0x400
#define PP_IDE_DMA_LENGTH       0x408
#define PP_IDE_DMA_ADDR         0x40c

#define PP_IDE_CFG_INTRQ        (1 << 3)

#define PP_IDE_DMA_GO           (1 << 0)
#define PP_IDE_DMA_ARM          (1 << 1)
#define PP_IDE_DMA_READ         (1 << 3)

static bool pp502x_ide_in(hwaddr offset, hwaddr base, hwaddr size)
{
    return offset >= base && offset < base + size;
}

/*
 * The drive's interrupt line. The IDE core raises it at the end of a transfer
 * and lowers it when the guest reads ATA status. We latch the rising edge into
 * IDE0_CFG for the DMA path, and pass the line itself through to the
 * interrupt controller for everything else.
 */
static void pp502x_ide_set_irq(void *opaque, int n, int level)
{
    PP502xIDEState *s = opaque;

    if (level) {
        s->intrq = true;
    }
    qemu_set_irq(s->irq, level);
}

static uint64_t pp502x_ide_read(void *opaque, hwaddr offset, unsigned size)
{
    PP502xIDEState *s = opaque;

    if (pp502x_ide_in(offset, PP_IDE_TASKFILE, PP_IDE_TASKFILE_SIZE)) {
        unsigned reg = (offset - PP_IDE_TASKFILE) >> 2;
        return reg ? ide_ioport_read(&s->bus, reg)
                   : ide_data_readw(&s->bus, 0);
    }

    if (pp502x_ide_in(offset, PP_IDE_CONTROL, PP_IDE_CONTROL_SIZE)) {
        return ide_status_read(&s->bus, 0);
    }

    if (offset == PP_IDE0_CFG) {
        return s->regs[offset / 4] | (s->intrq ? PP_IDE_CFG_INTRQ : 0);
    }

    return s->regs[offset / 4];
}

static void pp502x_ide_write(void *opaque, hwaddr offset, uint64_t value,
                             unsigned size)
{
    PP502xIDEState *s = opaque;
    uint32_t prev;

    if (pp502x_ide_in(offset, PP_IDE_TASKFILE, PP_IDE_TASKFILE_SIZE)) {
        unsigned reg = (offset - PP_IDE_TASKFILE) >> 2;
        if (reg) {
            ide_ioport_write(&s->bus, reg, value);
        } else {
            ide_data_writew(&s->bus, 0, value);
        }
        return;
    }

    if (pp502x_ide_in(offset, PP_IDE_CONTROL, PP_IDE_CONTROL_SIZE)) {
        ide_ctrl_write(&s->bus, 0, value);
        return;
    }

    switch (offset) {
    case PP_IDE0_CFG:
        /* Writing a one to INTRQ acknowledges it. */
        if (value & PP_IDE_CFG_INTRQ) {
            s->intrq = false;
        }
        s->regs[offset / 4] = value & ~PP_IDE_CFG_INTRQ;
        break;

    case PP_IDE_DMA_CONTROL:
        prev = s->regs[offset / 4];
        s->regs[offset / 4] = value;
        /*
         * The go bit going up is the transfer. If no ATA DMA command is
         * outstanding there is nothing to start, which is also what happens
         * on the way back down when the firmware clears the bit afterwards.
         */
        if ((value & PP_IDE_DMA_GO) && !(prev & PP_IDE_DMA_GO) && s->dma_cb) {
            s->dma_cb(ide_bus_active_if(&s->bus), 0);
        }
        break;

    default:
        s->regs[offset / 4] = value;
        break;
    }
}

static const MemoryRegionOps pp502x_ide_ops = {
    .read = pp502x_ide_read,
    .write = pp502x_ide_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

/*
 * IDEDMA. The core asks us to describe the transfer and then does the block
 * I/O itself; all we own is where in guest memory it lands.
 */
static void pp502x_ide_dma_start(const IDEDMA *dma, IDEState *is,
                                 BlockCompletionFunc *cb)
{
    PP502xIDEState *s = container_of(dma, PP502xIDEState, dma);

    s->dma_cb = cb;
    s->dma_cur = s->regs[PP_IDE_DMA_ADDR / 4];
    /* The length register holds the byte count minus four. */
    s->dma_left = s->regs[PP_IDE_DMA_LENGTH / 4] + 4;

    /* If the firmware armed it first, there is nothing left to wait for. */
    if (s->regs[PP_IDE_DMA_CONTROL / 4] & PP_IDE_DMA_GO) {
        cb(is, 0);
    }
}

static int32_t pp502x_ide_dma_prepare_buf(const IDEDMA *dma, int32_t limit)
{
    PP502xIDEState *s = container_of(dma, PP502xIDEState, dma);
    IDEState *is = ide_bus_active_if(&s->bus);
    uint32_t len = MIN((uint32_t)limit, s->dma_left);

    /*
     * One flat buffer, so one entry. If the firmware programmed a shorter
     * length than the ATA command needs, we hand back the short count and the
     * core treats it as the error it is rather than transferring past the end.
     */
    qemu_sglist_init(&is->sg, DEVICE(s), 1, &address_space_memory);
    if (len) {
        qemu_sglist_add(&is->sg, s->dma_cur, len);
        s->dma_cur += len;
        s->dma_left -= len;
    }

    return is->sg.size;
}

static void pp502x_ide_dma_set_inactive(const IDEDMA *dma, bool more)
{
    PP502xIDEState *s = container_of(dma, PP502xIDEState, dma);

    s->dma_cb = NULL;
}

static int pp502x_ide_dma_rw_buf(const IDEDMA *dma, bool is_write)
{
    /* Only used by the PIO fallback paths, which do not come through here. */
    return 0;
}

static void pp502x_ide_dma_reset(const IDEDMA *dma)
{
    PP502xIDEState *s = container_of(dma, PP502xIDEState, dma);

    s->dma_cb = NULL;
    s->dma_cur = 0;
    s->dma_left = 0;
}

static const IDEDMAOps pp502x_ide_dma_ops = {
    .start_dma    = pp502x_ide_dma_start,
    .prepare_buf  = pp502x_ide_dma_prepare_buf,
    .rw_buf       = pp502x_ide_dma_rw_buf,
    .set_inactive = pp502x_ide_dma_set_inactive,
    .reset        = pp502x_ide_dma_reset,
};

static void pp502x_ide_reset(DeviceState *dev)
{
    PP502xIDEState *s = PP502X_IDE(dev);

    ide_bus_reset(&s->bus);
    memset(s->regs, 0, sizeof(s->regs));
    s->intrq = false;
    s->dma_cb = NULL;
    s->dma_cur = 0;
    s->dma_left = 0;
}

static void pp502x_ide_realize(DeviceState *dev, Error **errp)
{
    PP502xIDEState *s = PP502X_IDE(dev);

    ide_bus_init_output_irq(&s->bus,
                            qemu_allocate_irq(pp502x_ide_set_irq, s, 0));

    /*
     * After ide_bus_init_output_irq, not before: the last thing it does is
     * bus->dma = &ide_dma_nop, so claiming the bus any earlier gets silently
     * undone and every DMA command is accepted and then ignored.
     */
    s->dma.ops = &pp502x_ide_dma_ops;
    s->bus.dma = &s->dma;

    memory_region_init_io(&s->iomem, OBJECT(s), &pp502x_ide_ops, s,
                          "pp502x.ide", PP502X_IDE_REGION_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
}

static void pp502x_ide_init(Object *obj)
{
    PP502xIDEState *s = PP502X_IDE(obj);

    ide_bus_init(&s->bus, sizeof(s->bus), DEVICE(obj), 0, 2);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

void pp502x_ide_init_drive(DeviceState *dev, DriveInfo *hd)
{
    PP502xIDEState *s = PP502X_IDE(dev);

    if (hd) {
        ide_bus_create_drive(&s->bus, 0, hd);
    }
}

static void pp502x_ide_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = pp502x_ide_realize;
    dc->reset = pp502x_ide_reset;
    /* Not migratable: the DMA cursor and the INTRQ latch are not saved. */
    dc->user_creatable = false;
}

static const TypeInfo pp502x_ide_type_info = {
    .name          = TYPE_PP502X_IDE,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PP502xIDEState),
    .instance_init = pp502x_ide_init,
    .class_init    = pp502x_ide_class_init,
};

static void pp502x_ide_register_types(void)
{
    type_register_static(&pp502x_ide_type_info);
}

type_init(pp502x_ide_register_types)
