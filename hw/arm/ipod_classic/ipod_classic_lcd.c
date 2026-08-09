/*
 * S5L8702 LCD interface.
 *
 * The bus side is modelled: the handshake bits every driver spins on, the
 * command and data ports, and a read cycle that completes. The panel on the
 * other side is NOT modelled -- commands and pixel words are accepted and
 * dropped, and a read cycle returns zero. Nothing here draws.
 *
 * That split is deliberate. The handshake is specified twice over (see the
 * header: Rockbox and Apple's retailOS spin on the same three bits), so it can
 * be written down with confidence. The panel is a different matter: which
 * controller is fitted is chosen at runtime from GPIO PDAT(6) bits [5:4], we
 * do not model GPIO yet, and guessing a command set would be inventing with
 * the appearance of rigour. Pixel writes are counted and reported once rather
 * than logged per word, because the boot pushes them by the hundred thousand.
 *
 * See include/hw/arm/ipod_classic/ipod_classic_lcd.h for the register map and
 * where every bit comes from.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "hw/arm/ipod_classic/ipod_classic_lcd.h"

static uint64_t ipod_classic_lcd_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    IPodClassicLCDState *s = opaque;

    switch (offset) {
    case S5L8702_LCD_STATUS:
        /*
         * Always idle and never busy. Every transfer we accept completes
         * inside the store that started it, so there is no window in which
         * the FIFO could be full -- and reporting busy here, or failing to
         * report idle, is an unbreakable spin in both drivers.
         */
        return S5L8702_LCD_STATUS_RESET |
               (s->dbuff_valid ? S5L8702_LCD_STATUS_RDATA : 0);

    case S5L8702_LCD_DBUFF:
        /*
         * Rockbox reads this as "*buf++ = LCD_DBUFF >> 1", so the panel's byte
         * sits above bit 0. We have no panel, so this is zero; the shift is
         * the driver's business either way.
         */
        s->dbuff_valid = false;
        return s->dbuff;

    case S5L8702_LCD_CON:
        return s->con;
    case S5L8702_LCD_INTCON:
        return s->intcon;
    case S5L8702_LCD_PHTIME:
        return s->phtime;
    case S5L8702_LCD_RST_TIME:
        return s->rst_time;
    case S5L8702_LCD_DRV_RST:
        return s->drv_rst;
    case S5L8702_LCD_RCMD:
        return s->rcmd;

    default:
        qemu_log_mask(LOG_UNIMP, "%s: read of +0x%" HWADDR_PRIx "\n", __func__,
                      offset);
        return 0;
    }
}

static void ipod_classic_lcd_write(void *opaque, hwaddr offset, uint64_t value,
                                   unsigned size)
{
    IPodClassicLCDState *s = opaque;

    if (offset >= S5L8702_LCD_WDATA && offset <= S5L8702_LCD_WDATA_LAST) {
        /*
         * Pixel data. Dropped -- see the file header. Counted so that the log
         * says how much was thrown away instead of saying nothing at all.
         */
        static uint64_t dropped;
        if (++dropped == 1) {
            qemu_log_mask(LOG_UNIMP, "%s: pixel data discarded, no panel is "
                          "modelled (further writes are silent)\n", __func__);
        }
        return;
    }

    switch (offset) {
    case S5L8702_LCD_CON:
        s->con = value;
        break;

    case S5L8702_LCD_WCMD:
        /*
         * A panel command. Which controller is listening depends on a strap we
         * do not read yet, so there is nothing meaningful to do with it.
         */
        qemu_log_mask(LOG_UNIMP, "%s: panel command 0x%" PRIx64
                      " ignored, no panel is modelled\n", __func__, value);
        break;

    case S5L8702_LCD_RCMD:
        s->rcmd = value;
        break;

    case S5L8702_LCD_RDATA:
        /*
         * Writing here starts a read cycle; the result appears in LCD_DBUFF
         * and bit 0 of LCD_STATUS goes up. With no panel the result is zero,
         * but the handshake still has to complete or the driver waits for
         * ever at lcd-s5l8702.c:241.
         */
        s->dbuff = 0;
        s->dbuff_valid = true;
        break;

    case S5L8702_LCD_INTCON:
        s->intcon = value;
        break;
    case S5L8702_LCD_PHTIME:
        s->phtime = value;
        break;
    case S5L8702_LCD_RST_TIME:
        s->rst_time = value;
        break;
    case S5L8702_LCD_DRV_RST:
        s->drv_rst = value;
        break;

    case S5L8702_LCD_STATUS:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: LCD_STATUS is read-only\n",
                      __func__);
        break;

    default:
        /*
         * +0x7C and +0x88 land here. The retailOS writes 0x804 and 0x1000000
         * to them once each, very early, and never reads them back; no source
         * we have names them and nothing observable depends on them, so they
         * are accepted and dropped. UNDETERMINED.
         */
        qemu_log_mask(LOG_UNIMP, "%s: write of 0x%" PRIx64 " to +0x%"
                      HWADDR_PRIx "\n", __func__, value, offset);
        break;
    }
}

static const MemoryRegionOps ipod_classic_lcd_ops = {
    .read = ipod_classic_lcd_read,
    .write = ipod_classic_lcd_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void ipod_classic_lcd_reset(DeviceState *dev)
{
    IPodClassicLCDState *s = IPOD_CLASSIC_LCD(dev);

    s->con = 0;
    s->intcon = 0;
    /* s5l87xx.h records the reset values of these two as 0x0060 and 0x07FF. */
    s->phtime = 0x0060;
    s->rst_time = 0x07FF;
    s->drv_rst = 0;
    s->dbuff = 0;
    s->dbuff_valid = false;
    s->rcmd = 0;
}

static void ipod_classic_lcd_realize(DeviceState *dev, Error **errp)
{
    IPodClassicLCDState *s = IPOD_CLASSIC_LCD(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &ipod_classic_lcd_ops, s,
                          TYPE_IPOD_CLASSIC_LCD, S5L8702_LCD_MEM_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);
}

static const VMStateDescription vmstate_ipod_classic_lcd = {
    .name = TYPE_IPOD_CLASSIC_LCD,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(con, IPodClassicLCDState),
        VMSTATE_UINT32(intcon, IPodClassicLCDState),
        VMSTATE_UINT32(phtime, IPodClassicLCDState),
        VMSTATE_UINT32(rst_time, IPodClassicLCDState),
        VMSTATE_UINT32(drv_rst, IPodClassicLCDState),
        VMSTATE_UINT32(dbuff, IPodClassicLCDState),
        VMSTATE_BOOL(dbuff_valid, IPodClassicLCDState),
        VMSTATE_UINT32(rcmd, IPodClassicLCDState),
        VMSTATE_END_OF_LIST()
    },
};

static void ipod_classic_lcd_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "S5L8702 LCD interface";
    dc->realize = ipod_classic_lcd_realize;
    dc->reset = ipod_classic_lcd_reset;
    dc->vmsd = &vmstate_ipod_classic_lcd;
    dc->user_creatable = false;
}

static const TypeInfo ipod_classic_lcd_info = {
    .name          = TYPE_IPOD_CLASSIC_LCD,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IPodClassicLCDState),
    .class_init    = ipod_classic_lcd_class_init,
};

static void ipod_classic_lcd_register_types(void)
{
    type_register_static(&ipod_classic_lcd_info);
}

type_init(ipod_classic_lcd_register_types)
