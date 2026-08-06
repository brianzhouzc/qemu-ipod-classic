/*
 * Broadcom BCM2722 video companion chip, as wired on the iPod 5G.
 *
 * The PP502x does not drive the panel itself. It talks to this chip through a
 * narrow window of registers 64 KB apart at 0x30000000, and the chip owns both
 * the framebuffer and the panel. From Rockbox's lcd-video.c:
 *
 *     bcm_write32(addr, val):  BCM_WR_ADDR32 = addr;
 *                              while (!(BCM_CONTROL & 0x2));
 *                              BCM_DATA32 = val;
 *
 *     bcm_read32(addr):        while (!(BCM_RD_ADDR & 1));
 *                              BCM_RD_ADDR32 = addr;
 *                              while (!(BCM_CONTROL & 0x10));
 *                              return BCM_DATA32;
 *
 * So: a pointer register, a data port that auto-increments, and ready bits.
 * A frame goes out as a run of 32-bit writes to the data port after pointing
 * the write pointer at BCMA_CMDPARAM plus the row offset, and is made visible
 * by writing BCMCMD_LCD_UPDATE to BCMA_COMMAND.
 *
 * What is modelled: the pointer/data/ready protocol, the chip's address space
 * as plain memory, and the two update commands, which push the framebuffer to
 * a QEMU console. What is not: TV out, YUV blitting, sleep, and the firmware
 * upload the real chip needs before it will do anything.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "ui/console.h"
#include "hw/arm/ipod_video/ipod_video_bcm.h"

/* Registers, which sit 64 KB apart. */
#define BCM_REG_DATA        0
#define BCM_REG_WR_ADDR     1
#define BCM_REG_RD_ADDR     2
#define BCM_REG_CONTROL     3
#define BCM_REG_ALT_DATA    4
#define BCM_REG_ALT_WR_ADDR 5
#define BCM_REG_ALT_RD_ADDR 6
#define BCM_REG_ALT_CONTROL 7

/* CONTROL bits the firmware spins on. */
#define BCM_CONTROL_WR_READY  0x02
#define BCM_CONTROL_RD_READY  0x10

/* Addresses inside the chip. */
#define BCMA_COMMAND    0x001F8
#define BCMA_STATUS     0x001FC
#define BCMA_CMDPARAM   0xE0000   /* where a frame is written */

/*
 * Commands are sent as a value and its complement in the two halves of a word,
 * which is what BCM_CMD(x) in lcd-video.c builds.
 */
#define BCM_CMD(x)              ((~((uint32_t)(x)) << 16) | ((uint32_t)(x)))
#define BCMCMD_LCD_UPDATE       BCM_CMD(0)
#define BCMCMD_LCD_UPDATERECT   BCM_CMD(5)

static void bcm_mem_write32(IPodVideoBCMState *s, uint32_t addr, uint32_t value)
{
    if (addr + 4 > BCM_MEM_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write past the end of the chip's memory (0x%x)\n",
                      __func__, addr);
        return;
    }
    stl_le_p(s->mem + addr, value);
}

static uint32_t bcm_mem_read32(IPodVideoBCMState *s, uint32_t addr)
{
    if (addr + 4 > BCM_MEM_SIZE) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read past the end of the chip's memory (0x%x)\n",
                      __func__, addr);
        return 0;
    }
    return ldl_le_p(s->mem + addr);
}

static void bcm_run_command(IPodVideoBCMState *s, uint32_t cmd)
{
    switch (cmd) {
    case BCMCMD_LCD_UPDATE:
    case BCMCMD_LCD_UPDATERECT:
        s->dirty = true;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: command 0x%08x is not modelled\n",
                      __func__, cmd);
        break;
    }
    /* Report the command as finished; the firmware polls STATUS for that. */
    stl_le_p(s->mem + BCMA_STATUS, 0);
}

static uint64_t ipod_video_bcm_read(void *opaque, hwaddr offset, unsigned size)
{
    IPodVideoBCMState *s = opaque;
    uint32_t value;

    switch (offset >> 16) {
    case BCM_REG_DATA:
    case BCM_REG_ALT_DATA:
        value = bcm_mem_read32(s, s->rd_addr);
        s->rd_addr += size;
        return value;

    case BCM_REG_RD_ADDR:
    case BCM_REG_ALT_RD_ADDR:
        /* Bit 0 says the chip will accept a new read address. It always will. */
        return 1;

    case BCM_REG_CONTROL:
    case BCM_REG_ALT_CONTROL:
        /* Never busy: there is no bus behind this to be slow. */
        return BCM_CONTROL_WR_READY | BCM_CONTROL_RD_READY;

    case BCM_REG_WR_ADDR:
    case BCM_REG_ALT_WR_ADDR:
        return s->wr_addr;

    default:
        qemu_log_mask(LOG_UNIMP, "%s: unknown register at +0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }
}

static void ipod_video_bcm_write(void *opaque, hwaddr offset, uint64_t value,
                                 unsigned size)
{
    IPodVideoBCMState *s = opaque;

    switch (offset >> 16) {
    case BCM_REG_DATA:
    case BCM_REG_ALT_DATA:
        bcm_mem_write32(s, s->wr_addr, value);
        /*
         * A command only takes effect once it lands in BCMA_COMMAND, so watch
         * for a write there rather than scanning the whole address space.
         */
        if (s->wr_addr == BCMA_COMMAND) {
            bcm_run_command(s, value);
        }
        s->wr_addr += size;
        break;

    case BCM_REG_WR_ADDR:
    case BCM_REG_ALT_WR_ADDR:
        s->wr_addr = value;
        break;

    case BCM_REG_RD_ADDR:
    case BCM_REG_ALT_RD_ADDR:
        s->rd_addr = value;
        break;

    case BCM_REG_CONTROL:
    case BCM_REG_ALT_CONTROL:
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "%s: unknown register at +0x%" HWADDR_PRIx
                      " (wrote 0x%" PRIx64 ")\n", __func__, offset, value);
        break;
    }
}

static const MemoryRegionOps ipod_video_bcm_ops = {
    .read = ipod_video_bcm_read,
    .write = ipod_video_bcm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 2,
    .valid.max_access_size = 4,
};

static void ipod_video_bcm_gfx_update(void *opaque)
{
    IPodVideoBCMState *s = opaque;

    if (!s->dirty) {
        return;
    }
    s->dirty = false;
    dpy_gfx_update_full(s->con);
}

static void ipod_video_bcm_invalidate(void *opaque)
{
    IPodVideoBCMState *s = opaque;

    s->dirty = true;
}

static const GraphicHwOps ipod_video_bcm_gfx_ops = {
    .invalidate = ipod_video_bcm_invalidate,
    .gfx_update = ipod_video_bcm_gfx_update,
};

static void ipod_video_bcm_reset(DeviceState *dev)
{
    IPodVideoBCMState *s = IPOD_VIDEO_BCM(dev);

    s->wr_addr = 0;
    s->rd_addr = 0;
    s->dirty = true;
    memset(s->mem, 0, BCM_MEM_SIZE);
}

static void ipod_video_bcm_realize(DeviceState *dev, Error **errp)
{
    IPodVideoBCMState *s = IPOD_VIDEO_BCM(dev);
    DisplaySurface *surface;

    s->con = graphic_console_init(dev, 0, &ipod_video_bcm_gfx_ops, s);
    qemu_console_resize(s->con, LCD_WIDTH, LCD_HEIGHT);

    /*
     * Point the console straight at the chip's memory where the firmware
     * writes pixels, so an update is just a repaint rather than a copy. The
     * panel is 320x240 RGB565, per Rockbox's config for this target.
     */
    surface = qemu_create_displaysurface_from(LCD_WIDTH, LCD_HEIGHT,
                                              PIXMAN_r5g6b5,
                                              LCD_WIDTH * 2,
                                              s->mem + BCMA_CMDPARAM);
    dpy_gfx_replace_surface(s->con, surface);
}

static void ipod_video_bcm_init(Object *obj)
{
    IPodVideoBCMState *s = IPOD_VIDEO_BCM(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    s->mem = g_malloc0(BCM_MEM_SIZE);

    memory_region_init_io(&s->iomem, obj, &ipod_video_bcm_ops, s,
                          TYPE_IPOD_VIDEO_BCM, BCM_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static const VMStateDescription vmstate_ipod_video_bcm = {
    .name = TYPE_IPOD_VIDEO_BCM,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(wr_addr, IPodVideoBCMState),
        VMSTATE_UINT32(rd_addr, IPodVideoBCMState),
        VMSTATE_END_OF_LIST()
    }
};

static void ipod_video_bcm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = ipod_video_bcm_realize;
    dc->reset = ipod_video_bcm_reset;
    dc->vmsd = &vmstate_ipod_video_bcm;
    dc->desc = "Broadcom BCM2722 video companion";
}

static const TypeInfo ipod_video_bcm_info = {
    .name          = TYPE_IPOD_VIDEO_BCM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IPodVideoBCMState),
    .instance_init = ipod_video_bcm_init,
    .class_init    = ipod_video_bcm_class_init,
};

static void ipod_video_bcm_register_types(void)
{
    type_register_static(&ipod_video_bcm_info);
}

type_init(ipod_video_bcm_register_types)
