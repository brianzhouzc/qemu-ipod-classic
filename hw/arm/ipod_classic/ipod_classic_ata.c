/*
 * S5L8702 ATA controller.
 *
 * PIO only. The two DMA descriptor pairs (TBUF/SBUF at 0x3c..0x50 with
 * ATA_XFR_NUM) store and read back but move nothing; see the note in
 * ipod_classic_ata_write(). Apple's retailOS identifies the disk over PIO
 * before it ever sets a DMA descriptor up, so this is enough to get the boot
 * past its storage probe, and going further needs a disk image we do not have.
 *
 * See include/hw/arm/ipod_classic/ipod_classic_ata.h for the bridge protocol,
 * which is the part of this block that is not obvious.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "sysemu/blockdev.h"
#include "hw/arm/ipod_classic/ipod_classic_ata.h"

/* Which IDE core port a device-register offset corresponds to. */
static int ata_ioport(hwaddr offset)
{
    return (offset - S5L8702_ATA_PIO_DTR) / 4;
}

static void ipod_classic_ata_update_irq(IPodClassicATAState *s)
{
    uint32_t active = s->reg[S5L8702_ATA_IRQSTAT / 4] &
                      s->reg[S5L8702_ATA_IRQMASK / 4];

    qemu_set_irq(s->irq, active != 0);
}

static void ipod_classic_ata_set_irq(void *opaque, int n, int level)
{
    IPodClassicATAState *s = opaque;

    s->intrq = level;
    if (level) {
        s->reg[S5L8702_ATA_IRQSTAT / 4] |= S5L8702_ATA_INTRQ_BIT;
    }
    ipod_classic_ata_update_irq(s);
}

static uint64_t ipod_classic_ata_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    IPodClassicATAState *s = opaque;

    switch (offset) {
    case S5L8702_ATA_CONTROL:
        /*
         * Bit 1 is the clock-down status: Rockbox's ata_power_down() writes
         * zero and then spins on it ("ATA_CONTROL = 0; while (!(ATA_CONTROL &
         * BIT(1)))"), so it has to come up as soon as the block is disabled.
         */
        if (s->reg[offset / 4] & S5L8702_ATA_CONTROL_ENABLE) {
            return s->reg[offset / 4] & ~S5L8702_ATA_CONTROL_CLOCKDOWN;
        }
        return s->reg[offset / 4] | S5L8702_ATA_CONTROL_CLOCKDOWN;

    case S5L8702_ATA_PIO_READY:
        return S5L8702_ATA_READY_CYCLE |
               (s->rdata_valid ? S5L8702_ATA_READY_RDATA : 0);

    case S5L8702_ATA_PIO_RDATA:
        s->rdata_valid = false;
        return s->rdata;

    case S5L8702_ATA_PIO_DTR:
    case S5L8702_ATA_PIO_FED:
    case S5L8702_ATA_PIO_SCR:
    case S5L8702_ATA_PIO_LLR:
    case S5L8702_ATA_PIO_LMR:
    case S5L8702_ATA_PIO_LHR:
    case S5L8702_ATA_PIO_DVR:
    case S5L8702_ATA_PIO_CSD:
    case S5L8702_ATA_PIO_DAD:
        /*
         * The dummy read that starts the cycle. Its own value is thrown away
         * by both drivers; what matters is that the result is latched where
         * ATA_PIO_RDATA will find it.
         */
        if (offset == S5L8702_ATA_PIO_DTR) {
            s->rdata = ide_data_readw(&s->bus, 0);
        } else if (offset == S5L8702_ATA_PIO_DAD) {
            /* Alternate status: same value as the status register, no side
             * effect on the device's interrupt. */
            s->rdata = ide_status_read(&s->bus, 0);
        } else {
            s->rdata = ide_ioport_read(&s->bus, ata_ioport(offset));
        }
        s->rdata_valid = true;
        return s->rdata;

    default:
        if (offset < S5L8702_ATA_PIO_DTR) {
            return s->reg[offset / 4];
        }
        qemu_log_mask(LOG_UNIMP, "%s: read of 0x%" HWADDR_PRIx "\n", __func__,
                      offset);
        return 0;
    }
}

static void ipod_classic_ata_write(void *opaque, hwaddr offset,
                                   uint64_t value, unsigned size)
{
    IPodClassicATAState *s = opaque;

    switch (offset) {
    case S5L8702_ATA_SWRST:
        s->reg[offset / 4] = value;
        if (value & 1) {
            ide_bus_reset(&s->bus);
            s->rdata_valid = false;
        }
        break;

    case S5L8702_ATA_IRQSTAT:
        s->reg[offset / 4] &= ~(uint32_t)value;
        ipod_classic_ata_update_irq(s);
        break;

    case S5L8702_ATA_IRQMASK:
        s->reg[offset / 4] = value;
        ipod_classic_ata_update_irq(s);
        break;

    case S5L8702_ATA_PIO_DTR:
        ide_data_writew(&s->bus, 0, value);
        break;
    case S5L8702_ATA_PIO_DAD:
        ide_ctrl_write(&s->bus, 0, value);
        break;
    case S5L8702_ATA_PIO_FED:
    case S5L8702_ATA_PIO_SCR:
    case S5L8702_ATA_PIO_LLR:
    case S5L8702_ATA_PIO_LMR:
    case S5L8702_ATA_PIO_LHR:
    case S5L8702_ATA_PIO_DVR:
    case S5L8702_ATA_PIO_CSD:
        /* Port 7 is the command register; the IDE core executes on write. */
        ide_ioport_write(&s->bus, ata_ioport(offset), value);
        break;

    case S5L8702_ATA_XFR_NUM:
        /*
         * The start of a DMA transfer on real silicon. Nothing here moves a
         * byte, so say so rather than accept it silently: a guest that got
         * this far would wait for a completion that never comes.
         */
        qemu_log_mask(LOG_UNIMP, "%s: DMA is not modelled (XFR_NUM = 0x%"
                      PRIx64 ")\n", __func__, value);
        s->reg[offset / 4] = value;
        break;

    case S5L8702_ATA_PIO_READY:
    case S5L8702_ATA_PIO_RDATA:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: 0x%" HWADDR_PRIx " is read-only\n",
                      __func__, offset);
        break;

    default:
        if (offset < S5L8702_ATA_PIO_DTR) {
            s->reg[offset / 4] = value;
        } else {
            qemu_log_mask(LOG_UNIMP, "%s: write of 0x%" PRIx64 " to 0x%"
                          HWADDR_PRIx "\n", __func__, value, offset);
        }
        break;
    }
}

static const MemoryRegionOps ipod_classic_ata_ops = {
    .read = ipod_classic_ata_read,
    .write = ipod_classic_ata_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static void ipod_classic_ata_reset(DeviceState *dev)
{
    IPodClassicATAState *s = IPOD_CLASSIC_ATA(dev);

    ide_bus_reset(&s->bus);
    memset(s->reg, 0, sizeof(s->reg));
    s->rdata = 0;
    s->rdata_valid = false;
    s->intrq = false;
}

static void ipod_classic_ata_realize(DeviceState *dev, Error **errp)
{
    IPodClassicATAState *s = IPOD_CLASSIC_ATA(dev);

    ide_bus_init_output_irq(&s->bus,
                            qemu_allocate_irq(ipod_classic_ata_set_irq, s, 0));

    memory_region_init_io(&s->iomem, OBJECT(s), &ipod_classic_ata_ops, s,
                          TYPE_IPOD_CLASSIC_ATA, S5L8702_ATA_MEM_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
}

static void ipod_classic_ata_init(Object *obj)
{
    IPodClassicATAState *s = IPOD_CLASSIC_ATA(obj);

    /* One channel, one device: the iPod has a single disk and no slave. */
    ide_bus_init(&s->bus, sizeof(s->bus), DEVICE(obj), 0, 1);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

void ipod_classic_ata_init_drive(DeviceState *dev, DriveInfo *hd)
{
    IPodClassicATAState *s = IPOD_CLASSIC_ATA(dev);

    if (hd) {
        ide_bus_create_drive(&s->bus, 0, hd);
    }
}

static void ipod_classic_ata_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "S5L8702 ATA controller";
    dc->realize = ipod_classic_ata_realize;
    dc->reset = ipod_classic_ata_reset;
    /* Not migratable: the latched read result is not saved. */
    dc->user_creatable = false;
}

static const TypeInfo ipod_classic_ata_info = {
    .name          = TYPE_IPOD_CLASSIC_ATA,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IPodClassicATAState),
    .instance_init = ipod_classic_ata_init,
    .class_init    = ipod_classic_ata_class_init,
};

static void ipod_classic_ata_register_types(void)
{
    type_register_static(&ipod_classic_ata_info);
}

type_init(ipod_classic_ata_register_types)
