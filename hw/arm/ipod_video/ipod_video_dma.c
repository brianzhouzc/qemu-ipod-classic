/*
 * The PP502x DMA engine.
 *
 * There are two identical blocks, register for register:
 *
 *   block A   master 0x60008000, channels 0x60009000 + n*0x20, IRQ 27
 *   block B   master 0x6000a000, channels 0x6000b000 + n*0x20, IRQ 26
 *
 * Only block B is in Rockbox's pp5020.h (DMA0..DMA3 at 0x6000b000 and
 * DMA_MASTER_CONTROL at 0x6000a000, DMA_IRQ 26), and Rockbox only ever uses
 * channel 0 of it, to feed the I2S FIFO. Block A appears in no public source;
 * it is documented only by Apple's own firmware, which is what uses it -- the
 * ~200 KB upload of the Broadcom's VMCS firmware goes out through block A
 * channel 0.
 *
 * The interrupt numbers are not guesswork. Apple's DMA device registers both
 * handlers at 0x1d98d0:
 *
 *     mov r1, #27 ; r2 = 0x2680c4 (b 0x1d9c40, block B's handler) ; register
 *     mov r1, #28 ; r2 = 0x2680c0 (b 0x1d9be0, block A's handler) ; register
 *
 * and 27/28 are indices into the firmware's own interrupt table, not hardware
 * lines. The table is built at 0x1fcaa0, entry n at object+0x208+4n:
 *
 *     0x1fcc38  entry 26 = -1 (no such line), entry 27 = 26
 *     0x1fcc44  entry 28 = 27
 *
 * So block B is hardware IRQ 26 -- which is exactly Rockbox's DMA_IRQ for the
 * block it documents, an independent confirmation -- and block A is 27. The
 * running machine agrees: CPU_INT_EN_STAT reads 0xcc002017 with the retailOS
 * up, which has both 26 and 27 set and 28 clear.
 *
 * A transfer, in the order the firmware programs it (Apple 0x28e248 and
 * 0x28ea14, Rockbox pcm-pp.c:105-153):
 *
 *     MASTER_CONTROL |= 1<<31              enable the block
 *     CMD &= ~START                        make sure the channel is stopped
 *     CMD: SINGLE set, everything else clear
 *     CMD |= RAM_TO_PER                    direction
 *     RAM_ADDR = source, PER_ADDR = port
 *     CMD = (CMD & 0xffff0000) | (bytes-4)
 *     read STATUS                          clears any interrupt still latched
 *     CMD |= INTR
 *     CMD |= START                         and only this moves anything
 *
 * The peripheral address is a port, not a range: only RAM_ADDR walks. That is
 * INFERRED rather than stated anywhere, but it is the only reading compatible
 * with both real users -- Rockbox aims PER_ADDR at the I2S write FIFO and
 * Apple aims it at the Broadcom's data port, and both are single addresses
 * that consume a stream.
 *
 * What is not modelled: peripheral request lines. A channel with WAIT_REQ set
 * is waiting for a peripheral to ask for data, and no peripheral here asks, so
 * such a channel is left alone rather than run at full speed. That is what
 * keeps Rockbox's audio path behaving exactly as it did before this file
 * existed: it sets WAIT_REQ, so nothing happens, which is what happened when
 * these registers were an unimplemented stub.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "exec/address-spaces.h"
#include "migration/vmstate.h"
#include "hw/arm/ipod_video/ipod_video_dma.h"

/* Master registers, from pp5020.h:630-633. */
#define PP_DMA_MASTER_CONTROL   0x00
#define PP_DMA_MASTER_STATUS    0x04
#define PP_DMA_REQ_STATUS       0x08

#define PP_DMA_MASTER_CONTROL_EN    (1u << 31)
/* Bit 24+n says channel n is running, per pp5020.h:637-640. */
#define PP_DMA_MASTER_STATUS_CH0    24

/* Channel registers, from pp5020.h:642-647. */
#define PP_DMA_CMD          0x00
#define PP_DMA_STATUS       0x04
#define PP_DMA_RAM_ADDR     0x10
#define PP_DMA_RAM_DESC     0x14    /* Rockbox calls this FLAGS */
#define PP_DMA_PER_ADDR     0x18
#define PP_DMA_PER_DESC     0x1c    /* Rockbox calls this INCR */

/*
 * CMD bits, pp5020.h:669-679. The count field holds bytes minus four with the
 * bottom two bits ignored, which is why the largest single transfer is 64 KB:
 * 0xfffc + 4. Apple splits its firmware upload into 64 KB pieces for exactly
 * that reason (0x286ff4 rounds the length up and steps by 0x10000).
 */
#define PP_DMA_CMD_SIZE         0xfffcu
#define PP_DMA_CMD_REQ_ID       (0xfu << 16)
#define PP_DMA_CMD_WAIT_REQ     (1u << 24)
#define PP_DMA_CMD_SINGLE       (1u << 26)
#define PP_DMA_CMD_RAM_TO_PER   (1u << 27)
#define PP_DMA_CMD_INTR         (1u << 30)
#define PP_DMA_CMD_START        (1u << 31)

/* STATUS bits, pp5020.h:681-683. */
#define PP_DMA_STATUS_REMAIN    0xfffcu
#define PP_DMA_STATUS_INTR      (1u << 30)
#define PP_DMA_STATUS_BUSY      (1u << 31)

/*
 * Both descriptor registers -- the memory side at +0x14 and the peripheral
 * side at +0x1c -- share one layout. Apple's init applies the same three masks
 * (0x70000, 0x7000000, 0x70000000) to both, at 0x28eeb4 onwards.
 *
 * Bits 30:28 are the access width, 0 = 8 bit, 1 = 16, 2 = 32 (pp5020.h:698).
 * Apple's property setter maps its argument 1 -> 0, 2 -> 1, 4 -> 2 at
 * 0x28e590, so the field counts bytes as a power of two.
 *
 * Bits 18:16 are a burst length and 26:24 are undetermined; neither changes
 * how many bytes move or where, so neither is acted on here.
 */
#define PP_DMA_DESC_WIDTH_SHIFT 28
#define PP_DMA_DESC_WIDTH_MASK  (0x7u << PP_DMA_DESC_WIDTH_SHIFT)

static unsigned pp502x_dma_width(uint32_t desc)
{
    uint32_t code = (desc & PP_DMA_DESC_WIDTH_MASK) >> PP_DMA_DESC_WIDTH_SHIFT;

    switch (code) {
    case 0:
        return 1;
    case 1:
        return 2;
    case 2:
        return 4;
    default:
        qemu_log_mask(LOG_UNIMP, "pp502x-dma: access width code %u is reserved;"
                      " treating it as 32 bits\n", code);
        return 4;
    }
}

/*
 * The line is shared by every channel in the block, and the handler works out
 * which one it was by reading each channel's STATUS and testing bit 30 --
 * Apple at 0x1d9bf4/0x1d9bf8, walking its own list of active channels. So the
 * line is simply the OR of the latched interrupts.
 */
static void pp502x_dma_update_irq(PP502xDMAState *s)
{
    bool level = false;

    for (int i = 0; i < PP502X_DMA_NUM_CHANNELS; i++) {
        if (s->chan[i].status & PP_DMA_STATUS_INTR) {
            level = true;
        }
    }
    qemu_set_irq(s->irq, level);
}

static void pp502x_dma_run(PP502xDMAState *s, int n)
{
    PP502xDMAChannel *c = &s->chan[n];
    uint32_t count = (c->cmd & PP_DMA_CMD_SIZE) + 4;
    bool ram_to_per = c->cmd & PP_DMA_CMD_RAM_TO_PER;
    unsigned width = pp502x_dma_width(c->per_desc);
    MemTxResult res = MEMTX_OK;
    uint8_t *buf;

    /*
     * A channel told to wait for a peripheral request has not been told to go
     * yet; the peripheral has. Nothing on this machine drives a request line,
     * so there is nothing to answer and the transfer stays pending. Rockbox's
     * playback path lands here, and did nothing before this file existed
     * either.
     */
    if (c->cmd & PP_DMA_CMD_WAIT_REQ) {
        qemu_log_mask(LOG_UNIMP, "%s: channel %d waits for request id %u, and"
                      " no peripheral request lines are modelled\n",
                      __func__, n, (c->cmd & PP_DMA_CMD_REQ_ID) >> 16);
        return;
    }

    /*
     * The block enable is a real gate on the hardware and both firmwares set
     * it before use (Apple at 0x1da... through its master object, Rockbox at
     * pcm-pp.c:111). It is reported rather than enforced: refusing the
     * transfer would turn a modelling mistake into a silent hang, which is the
     * failure mode this port has spent the most time on.
     */
    if (!(s->master_control & PP_DMA_MASTER_CONTROL_EN)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: channel %d started with the block disabled\n",
                      __func__, n);
    }

    buf = g_malloc(count);

    if (ram_to_per) {
        res = address_space_read(&address_space_memory, c->ram_addr,
                                 MEMTXATTRS_UNSPECIFIED, buf, count);
        for (uint32_t off = 0; off < count && res == MEMTX_OK; off += width) {
            switch (width) {
            case 1:
                address_space_stb(&address_space_memory, c->per_addr, buf[off],
                                  MEMTXATTRS_UNSPECIFIED, &res);
                break;
            case 2:
                address_space_stw_le(&address_space_memory, c->per_addr,
                                     lduw_le_p(buf + off),
                                     MEMTXATTRS_UNSPECIFIED, &res);
                break;
            default:
                address_space_stl_le(&address_space_memory, c->per_addr,
                                     ldl_le_p(buf + off),
                                     MEMTXATTRS_UNSPECIFIED, &res);
                break;
            }
        }
    } else {
        for (uint32_t off = 0; off < count && res == MEMTX_OK; off += width) {
            switch (width) {
            case 1:
                buf[off] = address_space_ldub(&address_space_memory,
                                              c->per_addr,
                                              MEMTXATTRS_UNSPECIFIED, &res);
                break;
            case 2:
                stw_le_p(buf + off,
                         address_space_lduw_le(&address_space_memory,
                                               c->per_addr,
                                               MEMTXATTRS_UNSPECIFIED, &res));
                break;
            default:
                stl_le_p(buf + off,
                         address_space_ldl_le(&address_space_memory,
                                              c->per_addr,
                                              MEMTXATTRS_UNSPECIFIED, &res));
                break;
            }
        }
        if (res == MEMTX_OK) {
            res = address_space_write(&address_space_memory, c->ram_addr,
                                      MEMTXATTRS_UNSPECIFIED, buf, count);
        }
    }

    g_free(buf);

    if (res != MEMTX_OK) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: channel %d failed moving 0x%x bytes"
                      " between 0x%08x and 0x%08x\n", __func__, n, count,
                      c->ram_addr, c->per_addr);
    }

    /*
     * Done, and nothing left to count. START is deliberately left set: the
     * firmware clears it itself when a chain of transfers ends (Apple at
     * 0x1d9ecc, "bic r2, r2, #0x80000000") and Rockbox reads CMD back expecting
     * to find its own configuration still there (pcm-pp.c:161). Whether the
     * hardware also clears it on completion is UNDETERMINED; leaving it set is
     * what both firmwares are written against.
     */
    c->status = PP_DMA_STATUS_INTR;
    if (c->cmd & PP_DMA_CMD_INTR) {
        pp502x_dma_update_irq(s);
    }
}

static uint64_t pp502x_dma_read(void *opaque, hwaddr offset, unsigned size)
{
    PP502xDMAState *s = opaque;
    uint32_t value;
    int n;

    if (offset < PP502X_DMA_CHANNEL_BASE) {
        switch (offset) {
        case PP_DMA_MASTER_CONTROL:
            return s->master_control;

        case PP_DMA_MASTER_STATUS:
            value = 0;
            for (int i = 0; i < PP502X_DMA_NUM_CHANNELS; i++) {
                if (s->chan[i].status & PP_DMA_STATUS_BUSY) {
                    value |= 1u << (PP_DMA_MASTER_STATUS_CH0 + i);
                }
            }
            return value;

        case PP_DMA_REQ_STATUS:
            return s->req_status;

        default:
            qemu_log_mask(LOG_UNIMP, "%s: unknown master register +0x%"
                          HWADDR_PRIx "\n", __func__, offset);
            return 0;
        }
    }

    n = (offset - PP502X_DMA_CHANNEL_BASE) / PP502X_DMA_CHANNEL_STRIDE;
    if (n >= PP502X_DMA_NUM_CHANNELS) {
        qemu_log_mask(LOG_UNIMP, "%s: channel %d is beyond the %d modelled\n",
                      __func__, n, PP502X_DMA_NUM_CHANNELS);
        return 0;
    }

    switch (offset % PP502X_DMA_CHANNEL_STRIDE) {
    case PP_DMA_CMD:
        return s->chan[n].cmd;

    case PP_DMA_STATUS:
        /*
         * Reading STATUS is how the interrupt is acknowledged -- there is no
         * separate clear register. This is not an assumption: it is the reason
         * for the warning at the top of pcm-pp.c, "DMA status cannot be viewed
         * from outside code in control because that can clear the interrupt
         * from outside the handler and prevent the handler from being called",
         * and both handlers rely on it (Rockbox pcm-pp.c:203, Apple 0x1d9bf4).
         */
        value = s->chan[n].status;
        s->chan[n].status &= ~PP_DMA_STATUS_INTR;
        pp502x_dma_update_irq(s);
        return value;

    case PP_DMA_RAM_ADDR:
        return s->chan[n].ram_addr;
    case PP_DMA_RAM_DESC:
        return s->chan[n].ram_desc;
    case PP_DMA_PER_ADDR:
        return s->chan[n].per_addr;
    case PP_DMA_PER_DESC:
        return s->chan[n].per_desc;

    default:
        qemu_log_mask(LOG_UNIMP, "%s: unknown channel register +0x%"
                      HWADDR_PRIx "\n", __func__,
                      offset % PP502X_DMA_CHANNEL_STRIDE);
        return 0;
    }
}

static void pp502x_dma_write(void *opaque, hwaddr offset, uint64_t value,
                             unsigned size)
{
    PP502xDMAState *s = opaque;
    uint32_t prev;
    int n;

    if (offset < PP502X_DMA_CHANNEL_BASE) {
        switch (offset) {
        case PP_DMA_MASTER_CONTROL:
            s->master_control = value;
            break;
        case PP_DMA_REQ_STATUS:
            s->req_status = value;
            break;
        case PP_DMA_MASTER_STATUS:
            break;
        default:
            qemu_log_mask(LOG_UNIMP, "%s: unknown master register +0x%"
                          HWADDR_PRIx " (wrote 0x%" PRIx64 ")\n",
                          __func__, offset, value);
            break;
        }
        return;
    }

    n = (offset - PP502X_DMA_CHANNEL_BASE) / PP502X_DMA_CHANNEL_STRIDE;
    if (n >= PP502X_DMA_NUM_CHANNELS) {
        qemu_log_mask(LOG_UNIMP, "%s: channel %d is beyond the %d modelled\n",
                      __func__, n, PP502X_DMA_NUM_CHANNELS);
        return;
    }

    switch (offset % PP502X_DMA_CHANNEL_STRIDE) {
    case PP_DMA_CMD:
        prev = s->chan[n].cmd;
        s->chan[n].cmd = value;
        /* The rising edge of START is the transfer. */
        if ((value & PP_DMA_CMD_START) && !(prev & PP_DMA_CMD_START)) {
            pp502x_dma_run(s, n);
        }
        break;

    case PP_DMA_STATUS:
        /* Read-only in both drivers; neither ever writes it. */
        qemu_log_mask(LOG_GUEST_ERROR, "%s: channel %d STATUS is not writable"
                      " (wrote 0x%" PRIx64 ")\n", __func__, n, value);
        break;

    case PP_DMA_RAM_ADDR:
        s->chan[n].ram_addr = value;
        break;
    case PP_DMA_RAM_DESC:
        s->chan[n].ram_desc = value;
        break;
    case PP_DMA_PER_ADDR:
        s->chan[n].per_addr = value;
        break;
    case PP_DMA_PER_DESC:
        s->chan[n].per_desc = value;
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "%s: unknown channel register +0x%"
                      HWADDR_PRIx " (wrote 0x%" PRIx64 ")\n", __func__,
                      offset % PP502X_DMA_CHANNEL_STRIDE, value);
        break;
    }
}

static const MemoryRegionOps pp502x_dma_ops = {
    .read = pp502x_dma_read,
    .write = pp502x_dma_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static void pp502x_dma_reset(DeviceState *dev)
{
    PP502xDMAState *s = PP502X_DMA(dev);

    s->master_control = 0;
    s->req_status = 0;
    memset(s->chan, 0, sizeof(s->chan));
    qemu_set_irq(s->irq, 0);
}

static void pp502x_dma_init(Object *obj)
{
    PP502xDMAState *s = PP502X_DMA(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &pp502x_dma_ops, s,
                          TYPE_PP502X_DMA, PP502X_DMA_REGION_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static const VMStateDescription vmstate_pp502x_dma_channel = {
    .name = "pp502x_dma_channel",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(cmd, PP502xDMAChannel),
        VMSTATE_UINT32(status, PP502xDMAChannel),
        VMSTATE_UINT32(ram_addr, PP502xDMAChannel),
        VMSTATE_UINT32(ram_desc, PP502xDMAChannel),
        VMSTATE_UINT32(per_addr, PP502xDMAChannel),
        VMSTATE_UINT32(per_desc, PP502xDMAChannel),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_pp502x_dma = {
    .name = TYPE_PP502X_DMA,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(master_control, PP502xDMAState),
        VMSTATE_UINT32(req_status, PP502xDMAState),
        VMSTATE_STRUCT_ARRAY(chan, PP502xDMAState, PP502X_DMA_NUM_CHANNELS, 1,
                             vmstate_pp502x_dma_channel, PP502xDMAChannel),
        VMSTATE_END_OF_LIST()
    }
};

static void pp502x_dma_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = pp502x_dma_reset;
    dc->vmsd = &vmstate_pp502x_dma;
    dc->desc = "PP502x DMA engine";
    dc->user_creatable = false;
}

static const TypeInfo pp502x_dma_info = {
    .name          = TYPE_PP502X_DMA,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PP502xDMAState),
    .instance_init = pp502x_dma_init,
    .class_init    = pp502x_dma_class_init,
};

static void pp502x_dma_register_types(void)
{
    type_register_static(&pp502x_dma_info);
}

type_init(pp502x_dma_register_types)
