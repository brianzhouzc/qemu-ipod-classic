/*
 * The PP502x I2C controller.
 *
 * This is not a bit-banged or byte-at-a-time engine: the block takes a slave
 * address, up to four data bytes and a length, and runs the whole transaction
 * when the start bit is written. Both firmwares drive it identically --
 * Rockbox's i2c-pp.c and Apple's driver at 0x282d90/0x282f58 in
 * apple_retailos.bin are the same five steps in the same order:
 *
 *     ADDR  = (slave7 << 1) | read     -- the first byte on the wire
 *     CTRL  = read bit and (len - 1)
 *     DATA0..3 = the bytes to send, on a write
 *     CTRL |= 0x80                     -- SEND, and the transfer happens
 *     wait for STATUS bit 6 (BUSY) to fall, then read DATA0..3 back
 *
 * Neither firmware ever uses the interrupt, so this raises none -- and on this
 * SoC I2C_IRQ (40) is the click wheel's line anyway.
 *
 * Three things are modelled here that Rockbox alone would not have shown, all
 * read out of Apple's driver:
 *
 *   - STATUS bits 2:0 are a result code. 0x282f44 does "and r0, r0, #7" and
 *     the caller at 0x28358c aborts the whole transaction if it is not zero.
 *     So a successful transfer has to leave those three bits clear; a flat
 *     register file happened to do that by accident, which is why the guest
 *     never noticed there was no controller here.
 *
 *   - +0x08 is a second address register, used only by the combined
 *     write-then-read at 0x282e30: ADDR gets the write address, DATA0 the byte
 *     to write, +0x08 the read address, CTRL 0x48, and the byte that comes
 *     back lands in DATA1 rather than DATA0. Rockbox never does this.
 *
 *   - The block has to work with no initialisation at all. Apple's
 *     "DEV_EN |= DEV_I2C, pulse DEV_RS, write 0x600060a4" routine at 0x282e98
 *     is reached only from 0x282f0c, which calls it when a transfer times out.
 *     If our STATUS never says BUSY, that path never runs and the guest never
 *     enables the block.
 *
 * What is deliberately not modelled: bits 6 and 3 of CTRL individually (they
 * are only ever seen together, as the 0x48 above), the meaning of result codes
 * 1..6, and the four registers at +0x20/+0x2c/+0x30/+0x3c that Apple's
 * 0xf910c writes -- its only caller is guarded by a byte that nothing else in
 * the image ever sets, so it is unreachable in this firmware.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "migration/vmstate.h"
#include "hw/i2c/i2c.h"
#include "hw/arm/ipod_video/ipod_video_i2c.h"

/* Offsets, from i2c-pp.c lines 39-48 plus +0x08 out of Apple's driver. */
#define PP_I2C_CTRL         0x00
#define PP_I2C_ADDR         0x04
#define PP_I2C_ADDR2        0x08
#define PP_I2C_DATA0        0x0c
#define PP_I2C_DATA(n)      (PP_I2C_DATA0 + 4 * (n))
#define PP_I2C_STATUS       0x1c

/* CTRL. Only SEND and BUSY have names in any public source. */
#define PP_I2C_CTRL_SEND        0x80
#define PP_I2C_CTRL_READ        0x20
#define PP_I2C_CTRL_LEN_MASK    0x06
#define PP_I2C_CTRL_LEN_SHIFT   1
/*
 * The one combined-transfer value the firmware ever writes (0x282e6c does
 * "mov r0, #72"). Which of bits 6 and 3 means "there is a read phase" and
 * which means "one byte" cannot be told apart from a single sample, so this is
 * matched whole rather than decoded.
 */
#define PP_I2C_CTRL_COMBINED    0x48

/* STATUS. */
#define PP_I2C_STATUS_BUSY      0x40
#define PP_I2C_STATUS_CODE      0x07

/*
 * A result code for "nobody answered". Only 0 is known to mean success; both
 * firmwares treat every non-zero code the same way, and Apple's reuses 7 for
 * its own timeout, so 1 here is a placeholder for a value we do not know
 * rather than a decoded one.
 */
#define PP_I2C_CODE_NAK         0x01

#define PP_I2C_MAX_LEN          4

static uint8_t pp502x_i2c_run(PP502xI2CState *s)
{
    uint8_t ctrl = s->regs[PP_I2C_CTRL];
    uint8_t addr = s->regs[PP_I2C_ADDR];
    unsigned len = ((ctrl & PP_I2C_CTRL_LEN_MASK) >> PP_I2C_CTRL_LEN_SHIFT) + 1;
    unsigned i;

    if ((ctrl & ~PP_I2C_CTRL_SEND) == PP_I2C_CTRL_COMBINED) {
        /*
         * Write one byte, repeated start, read one byte back. The received
         * byte appears in DATA1: the data registers are consumed in order, so
         * what was transmitted comes first and what was received follows it.
         */
        if (i2c_start_send(s->bus, addr >> 1)) {
            return PP_I2C_CODE_NAK;
        }
        if (i2c_send(s->bus, s->regs[PP_I2C_DATA(0)])) {
            i2c_end_transfer(s->bus);
            return PP_I2C_CODE_NAK;
        }
        if (i2c_start_recv(s->bus, s->regs[PP_I2C_ADDR2] >> 1)) {
            i2c_end_transfer(s->bus);
            return PP_I2C_CODE_NAK;
        }
        s->regs[PP_I2C_DATA(1)] = i2c_recv(s->bus);
        i2c_end_transfer(s->bus);
        return 0;
    }

    if (ctrl & PP_I2C_CTRL_READ) {
        if (i2c_start_recv(s->bus, addr >> 1)) {
            return PP_I2C_CODE_NAK;
        }
        for (i = 0; i < len; i++) {
            s->regs[PP_I2C_DATA(i)] = i2c_recv(s->bus);
        }
        i2c_end_transfer(s->bus);
        return 0;
    }

    if (i2c_start_send(s->bus, addr >> 1)) {
        return PP_I2C_CODE_NAK;
    }
    for (i = 0; i < len; i++) {
        if (i2c_send(s->bus, s->regs[PP_I2C_DATA(i)])) {
            i2c_end_transfer(s->bus);
            return PP_I2C_CODE_NAK;
        }
    }
    i2c_end_transfer(s->bus);
    return 0;
}

static uint64_t pp502x_i2c_read(void *opaque, hwaddr offset, unsigned size)
{
    PP502xI2CState *s = opaque;

    if (offset >= PP502X_I2C_NUM_REGS) {
        qemu_log_mask(LOG_UNIMP, "%s: read of +0x%" HWADDR_PRIx
                      ", which is past the end of the block\n",
                      __func__, offset);
        return 0;
    }
    return s->regs[offset];
}

static void pp502x_i2c_write(void *opaque, hwaddr offset, uint64_t value,
                             unsigned size)
{
    PP502xI2CState *s = opaque;

    if (offset >= PP502X_I2C_NUM_REGS) {
        qemu_log_mask(LOG_UNIMP, "%s: write of 0x%02x to +0x%" HWADDR_PRIx
                      ", which is past the end of the block\n",
                      __func__, (uint8_t)value, offset);
        return;
    }

    switch (offset) {
    case PP_I2C_STATUS:
        /* The controller owns this one; the guest only ever polls it. */
        break;

    case PP_I2C_CTRL:
        s->regs[PP_I2C_CTRL] = value;
        if (value & PP_I2C_CTRL_SEND) {
            s->regs[PP_I2C_STATUS] = pp502x_i2c_run(s) & PP_I2C_STATUS_CODE;
            /*
             * SEND has to clear itself, and that is derived rather than
             * assumed: every later step in both drivers is a read-modify-write
             * of CTRL ("I2C_CTRL |= 0x20", "bic r1, r1, #6"), so a start bit
             * that stayed set would fire a second, unasked-for transfer on the
             * next one of those -- and on a slave with an auto-incrementing
             * register pointer that reads the wrong bytes for ever after.
             *
             * BUSY is likewise never set: the transfer above already finished.
             * Nothing in either firmware can tell the difference, because both
             * only ever wait for it to fall.
             */
            s->regs[PP_I2C_CTRL] &= ~PP_I2C_CTRL_SEND;
        }
        break;

    default:
        s->regs[offset] = value;
        break;
    }
}

static const MemoryRegionOps pp502x_i2c_ops = {
    .read = pp502x_i2c_read,
    .write = pp502x_i2c_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    /*
     * Byte registers: both drivers use ldrb/strb. Word accesses are accepted
     * and split, which is what the hardware would do with four byte-wide
     * registers behind a 32-bit bus.
     */
    .impl.min_access_size = 1,
    .impl.max_access_size = 1,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

I2CBus *pp502x_i2c_get_bus(DeviceState *dev)
{
    return PP502X_I2C(dev)->bus;
}

static void pp502x_i2c_reset(DeviceState *dev)
{
    PP502xI2CState *s = PP502X_I2C(dev);

    memset(s->regs, 0, sizeof(s->regs));
}

static void pp502x_i2c_realize(DeviceState *dev, Error **errp)
{
    PP502xI2CState *s = PP502X_I2C(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &pp502x_i2c_ops, s,
                          TYPE_PP502X_I2C, PP502X_I2C_REGION_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
}

static void pp502x_i2c_init(Object *obj)
{
    PP502xI2CState *s = PP502X_I2C(obj);

    s->bus = i2c_init_bus(DEVICE(obj), "i2c");
}

static const VMStateDescription vmstate_pp502x_i2c = {
    .name = TYPE_PP502X_I2C,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT8_ARRAY(regs, PP502xI2CState, PP502X_I2C_NUM_REGS),
        VMSTATE_END_OF_LIST()
    }
};

static void pp502x_i2c_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = pp502x_i2c_realize;
    dc->reset = pp502x_i2c_reset;
    dc->vmsd = &vmstate_pp502x_i2c;
    dc->desc = "PortalPlayer PP502x I2C controller";
    dc->user_creatable = false;
}

static const TypeInfo pp502x_i2c_type_info = {
    .name          = TYPE_PP502X_I2C,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(PP502xI2CState),
    .instance_init = pp502x_i2c_init,
    .class_init    = pp502x_i2c_class_init,
};

static void pp502x_i2c_register_types(void)
{
    type_register_static(&pp502x_i2c_type_info);
}

type_init(pp502x_i2c_register_types)
