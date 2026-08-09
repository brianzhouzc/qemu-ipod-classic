/*
 * PP502x interrupt controller.
 *
 * Sixty-four sources in two banks of thirty-two: the low one at 0x60004000,
 * the high one at 0x60004100 with an identical register layout. Each bank
 * carries a separate enable mask per core, which is how the CPU and the COP
 * take different interrupts from the same set of sources.
 *
 * Register map from Rockbox's pp5020.h. Enabling and disabling are separate
 * write-only registers -- writing ones to CPU_INT_EN enables those sources and
 * writing ones to CPU_INT_DIS disables them -- with the current mask readable
 * back through CPU_INT_EN_STAT.
 *
 * Only the CPU's outputs are wired. The COP is not modelled, so its masks are
 * kept and reported faithfully but its interrupt line goes nowhere; nothing
 * runs there to take it.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "hw/arm/ipod_video/ipod_video_intc.h"

/* Offsets within a bank. */
#define INTC_CPU_INT_STAT   0x00
#define INTC_COP_INT_STAT   0x04
#define INTC_CPU_FIQ_STAT   0x08
#define INTC_COP_FIQ_STAT   0x0c
#define INTC_INT_STAT       0x10
#define INTC_FORCED_STAT    0x14
#define INTC_FORCED_SET     0x18
#define INTC_FORCED_CLR     0x1c
#define INTC_CPU_INT_EN_STAT 0x20
#define INTC_CPU_INT_EN     0x24
#define INTC_CPU_INT_DIS    0x28
#define INTC_CPU_INT_PRIORITY 0x2c
#define INTC_COP_INT_EN_STAT 0x30
#define INTC_COP_INT_EN     0x34
#define INTC_COP_INT_DIS    0x38
#define INTC_COP_INT_PRIORITY 0x3c

#define INTC_BANK_SIZE      0x100

static uint32_t bank_pending(const IPodVideoINTCBank *b)
{
    return (b->raw | b->forced);
}

static void ipod_video_intc_update(IPodVideoINTCState *s)
{
    bool irq = false;
    bool cop_irq = false;

    /*
     * Each core has its own enable mask over the same set of raw lines, which
     * is how the firmware hands a device to one core or the other.
     */
    for (int i = 0; i < INTC_NUM_BANKS; i++) {
        uint32_t pending = bank_pending(&s->bank[i]);

        if (pending & s->bank[i].cpu_enabled) {
            irq = true;
        }
        if (pending & s->bank[i].cop_enabled) {
            cop_irq = true;
        }
    }
    qemu_set_irq(s->irq, irq);
    qemu_set_irq(s->cop_irq, cop_irq);
}

static void ipod_video_intc_set_irq(void *opaque, int n, int level)
{
    IPodVideoINTCState *s = opaque;
    IPodVideoINTCBank *b = &s->bank[n / 32];
    uint32_t bit = 1u << (n % 32);

    if (level) {
        b->raw |= bit;
    } else {
        b->raw &= ~bit;
    }
    ipod_video_intc_update(s);
}

static uint64_t ipod_video_intc_read(void *opaque, hwaddr offset, unsigned size)
{
    IPodVideoINTCState *s = opaque;
    IPodVideoINTCBank *b = &s->bank[offset / INTC_BANK_SIZE];

    if (offset / INTC_BANK_SIZE >= INTC_NUM_BANKS) {
        return 0;
    }

    switch (offset % INTC_BANK_SIZE) {
    case INTC_CPU_INT_STAT:
        return bank_pending(b) & b->cpu_enabled;
    case INTC_COP_INT_STAT:
        return bank_pending(b) & b->cop_enabled;
    case INTC_CPU_FIQ_STAT:
    case INTC_COP_FIQ_STAT:
        /* Nothing is routed to FIQ; priority is not modelled. */
        return 0;
    case INTC_INT_STAT:
        return bank_pending(b);
    case INTC_FORCED_STAT:
        return b->forced;
    case INTC_CPU_INT_EN_STAT:
        return b->cpu_enabled;
    case INTC_COP_INT_EN_STAT:
        return b->cop_enabled;
    case INTC_CPU_INT_PRIORITY:
    case INTC_COP_INT_PRIORITY:
        return 0;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unknown register +0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }
}

static void ipod_video_intc_write(void *opaque, hwaddr offset, uint64_t value,
                                  unsigned size)
{
    IPodVideoINTCState *s = opaque;
    IPodVideoINTCBank *b = &s->bank[offset / INTC_BANK_SIZE];

    if (offset / INTC_BANK_SIZE >= INTC_NUM_BANKS) {
        return;
    }

    switch (offset % INTC_BANK_SIZE) {
    case INTC_FORCED_SET:
        b->forced |= value;
        break;
    case INTC_FORCED_CLR:
        b->forced &= ~(uint32_t)value;
        break;
    case INTC_CPU_INT_EN:
        b->cpu_enabled |= value;
        break;
    case INTC_CPU_INT_DIS:
        b->cpu_enabled &= ~(uint32_t)value;
        break;
    case INTC_COP_INT_EN:
        b->cop_enabled |= value;
        break;
    case INTC_COP_INT_DIS:
        b->cop_enabled &= ~(uint32_t)value;
        break;
    case INTC_CPU_INT_PRIORITY:
    case INTC_COP_INT_PRIORITY:
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unknown register +0x%" HWADDR_PRIx
                      " (wrote 0x%" PRIx64 ")\n", __func__, offset, value);
        return;
    }
    ipod_video_intc_update(s);
}

static const MemoryRegionOps ipod_video_intc_ops = {
    .read = ipod_video_intc_read,
    .write = ipod_video_intc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void ipod_video_intc_reset(DeviceState *dev)
{
    IPodVideoINTCState *s = IPOD_VIDEO_INTC(dev);

    memset(s->bank, 0, sizeof(s->bank));
    ipod_video_intc_update(s);
}

static void ipod_video_intc_init(Object *obj)
{
    IPodVideoINTCState *s = IPOD_VIDEO_INTC(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &ipod_video_intc_ops, s,
                          TYPE_IPOD_VIDEO_INTC, INTC_NUM_BANKS * INTC_BANK_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
    sysbus_init_irq(sbd, &s->cop_irq);
    qdev_init_gpio_in(DEVICE(obj), ipod_video_intc_set_irq,
                      INTC_NUM_BANKS * 32);
}

static const VMStateDescription vmstate_ipod_video_intc_bank = {
    .name = "ipod_video_intc_bank",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(raw, IPodVideoINTCBank),
        VMSTATE_UINT32(forced, IPodVideoINTCBank),
        VMSTATE_UINT32(cpu_enabled, IPodVideoINTCBank),
        VMSTATE_UINT32(cop_enabled, IPodVideoINTCBank),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_ipod_video_intc = {
    .name = TYPE_IPOD_VIDEO_INTC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(bank, IPodVideoINTCState, INTC_NUM_BANKS, 1,
                             vmstate_ipod_video_intc_bank, IPodVideoINTCBank),
        VMSTATE_END_OF_LIST()
    }
};

static void ipod_video_intc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = ipod_video_intc_reset;
    dc->vmsd = &vmstate_ipod_video_intc;
    dc->desc = "PP502x interrupt controller";
}

static const TypeInfo ipod_video_intc_info = {
    .name          = TYPE_IPOD_VIDEO_INTC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IPodVideoINTCState),
    .instance_init = ipod_video_intc_init,
    .class_init    = ipod_video_intc_class_init,
};

static void ipod_video_intc_register_types(void)
{
    type_register_static(&ipod_video_intc_info);
}

type_init(ipod_video_intc_register_types)
