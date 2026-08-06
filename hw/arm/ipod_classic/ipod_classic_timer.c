/*
 * S5L8702 timer block.
 *
 * See include/hw/arm/ipod_classic/ipod_classic_timer.h for where the register
 * layout comes from. Only interval mode is modelled, which is the only mode
 * Rockbox uses; anything else is logged as unimplemented rather than silently
 * doing the wrong thing.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "hw/arm/ipod_classic/ipod_classic_timer.h"

static bool chan_is_32bit(const IPodClassicTimerChannel *c)
{
    return c->index >= S5L8702_NUM_TIMERS_16;
}

/* Interrupt status bits (INT0, INT1, INTOVF) currently latched for a channel. */
static uint32_t chan_status(const IPodClassicTimerChannel *c)
{
    if (chan_is_32bit(c)) {
        uint32_t shift = S5L8702_TSTAT_SHIFT(c->index - S5L8702_NUM_TIMERS_16);
        return (c->parent->tstat >> shift) & S5L8702_TIMER_CON_INT_MASK;
    }
    return (c->con >> S5L8702_TIMER_CON_INT_STAT_SHIFT) &
           S5L8702_TIMER_CON_INT_MASK;
}

static uint32_t chan_enabled_ints(const IPodClassicTimerChannel *c)
{
    return (c->con >> S5L8702_TIMER_CON_INT_EN_SHIFT) &
           S5L8702_TIMER_CON_INT_MASK;
}

static void ipod_classic_timer_update_irq(IPodClassicTimerState *s)
{
    bool level16 = false, level32 = false;

    for (unsigned i = 0; i < S5L8702_NUM_TIMERS; i++) {
        IPodClassicTimerChannel *c = &s->chan[i];
        if (!(chan_status(c) & chan_enabled_ints(c))) {
            continue;
        }
        if (chan_is_32bit(c)) {
            level32 = true;
        } else {
            level16 = true;
        }
    }

    qemu_set_irq(s->irq16, level16);
    qemu_set_irq(s->irq32, level32);
}

/* Raise INT0 for a channel, i.e. "the counter reached DATA0". */
static void chan_set_int0(IPodClassicTimerChannel *c)
{
    if (chan_is_32bit(c)) {
        uint32_t shift = S5L8702_TSTAT_SHIFT(c->index - S5L8702_NUM_TIMERS_16);
        c->parent->tstat |= 1u << shift;
    } else {
        c->con |= 1u << S5L8702_TIMER_CON_INT_STAT_SHIFT;
    }
}

static void ipod_classic_timer_tick(void *opaque)
{
    IPodClassicTimerChannel *c = opaque;

    chan_set_int0(c);
    ipod_classic_timer_update_irq(c->parent);
}

/*
 * Source clock for a channel, from CON[10:8] (Tx_CS) and CON[6] (ECLK select).
 *
 *   Tx_CS   CON[6]=0        CON[6]=1
 *   000     PCLK / 2        ECLK / 2
 *   001     PCLK / 4        ECLK / 4
 *   010     PCLK / 16       ECLK / 16
 *   011     PCLK / 64       ECLK / 64
 *   10x     PCLK            ECLK            (timers E..H)
 *   10x     Ext. Clock 0    Ext. Clock 0    (timers A..D)
 *   11x     Ext. Clock 1    Ext. Clock 1
 *
 * On the iPod classic Ext. Clock 0 is not connected and Ext. Clock 1 is the
 * 32768 Hz oscillator. PCLK is not modelled yet: the clock block is a plain
 * register file, so we do not know the PLL settings the guest asked for. Both
 * Rockbox timers select ECLK, so treating PCLK as ECLK is enough for now and
 * is flagged when it happens.
 */
static uint32_t chan_freq(IPodClassicTimerChannel *c)
{
    uint32_t cs = (c->con >> S5L8702_TIMER_CON_CS_SHIFT) &
                  S5L8702_TIMER_CON_CS_MASK;
    uint32_t base = S5L8702_TIMER_ECLK_HZ;
    uint32_t div;

    /*
     * Only complain about a configuration the guest is actually going to run
     * with: drivers routinely clear the timer before writing CON, so a
     * half-configured channel is normal and not worth reporting.
     */
    bool running = c->cmd & S5L8702_TIMER_CMD_EN;

    if (running && !(c->con & S5L8702_TIMER_CON_ECLK)) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: timer %c selects PCLK, which is not modelled; "
                      "using ECLK (%u Hz)\n", __func__, 'A' + c->index, base);
    }

    switch (cs) {
    case 0: div = 2;  break;
    case 1: div = 4;  break;
    case 2: div = 16; break;
    case 3: div = 64; break;
    case 4:
    case 5:
        if (chan_is_32bit(c)) {
            div = 1;
        } else {
            qemu_log_mask(LOG_UNIMP,
                          "%s: timer %c selects Ext. Clock 0, which is not "
                          "connected on this board\n", __func__,
                          'A' + c->index);
            div = 1;
        }
        break;
    default:                    /* 11x: Ext. Clock 1, the 32768 Hz OSC1 */
        base = 32768;
        div = 1;
        break;
    }

    return base / div / (c->pre + 1);
}

static void chan_reload(IPodClassicTimerChannel *c)
{
    uint64_t limit = c->data0;
    uint32_t mode = (c->con >> S5L8702_TIMER_CON_MODE_SHIFT) & 0x3;

    if (mode != 0) {
        qemu_log_mask(LOG_UNIMP, "%s: timer %c mode %u is not modelled, "
                      "treating it as interval mode\n", __func__,
                      'A' + c->index, mode);
    }

    /* A zero period would make the ptimer fire continuously. */
    if (limit == 0) {
        limit = chan_is_32bit(c) ? 0x100000000ULL : 0x10000;
    }

    ptimer_transaction_begin(c->ptimer);
    ptimer_set_freq(c->ptimer, chan_freq(c));
    ptimer_set_limit(c->ptimer, limit, 1);
    if (c->cmd & S5L8702_TIMER_CMD_EN) {
        ptimer_run(c->ptimer, 0);       /* 0: reload and keep going */
    } else {
        ptimer_stop(c->ptimer);
    }
    ptimer_transaction_commit(c->ptimer);
}

/* Map an MMIO offset to a channel and a register, or return false. */
static bool decode_offset(hwaddr offset, unsigned *chan, hwaddr *reg)
{
    if (offset < S5L8702_TIMER_AD_BASE +
                 S5L8702_NUM_TIMERS_16 * S5L8702_TIMER_STRIDE) {
        *chan = (offset - S5L8702_TIMER_AD_BASE) / S5L8702_TIMER_STRIDE;
        *reg = offset % S5L8702_TIMER_STRIDE;
        return true;
    }
    if (offset >= S5L8702_TIMER_EH_BASE &&
        offset < S5L8702_TIMER_EH_BASE +
                 S5L8702_NUM_TIMERS_16 * S5L8702_TIMER_STRIDE) {
        *chan = S5L8702_NUM_TIMERS_16 +
                (offset - S5L8702_TIMER_EH_BASE) / S5L8702_TIMER_STRIDE;
        *reg = (offset - S5L8702_TIMER_EH_BASE) % S5L8702_TIMER_STRIDE;
        return true;
    }
    return false;
}

static uint64_t ipod_classic_timer_read(void *opaque, hwaddr offset,
                                        unsigned size)
{
    IPodClassicTimerState *s = opaque;
    unsigned ch;
    hwaddr reg;

    if (offset == S5L8702_TIMER_TSTAT) {
        return s->tstat;
    }

    if (!decode_offset(offset, &ch, &reg)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }

    IPodClassicTimerChannel *c = &s->chan[ch];

    switch (reg) {
    case S5L8702_TIMER_CON:
        return c->con;
    case S5L8702_TIMER_CMD:
        return c->cmd;
    case S5L8702_TIMER_DATA0:
        return c->data0;
    case S5L8702_TIMER_DATA1:
        return c->data1;
    case S5L8702_TIMER_PRE:
        return c->pre;
    case S5L8702_TIMER_CNT:
        /*
         * The hardware counts up towards DATA0; ptimer counts down towards
         * zero, so report the difference.
         */
        return c->data0 - ptimer_get_count(c->ptimer);
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad register 0x%" HWADDR_PRIx
                      " of timer %c\n", __func__, reg, 'A' + ch);
        return 0;
    }
}

static void ipod_classic_timer_write(void *opaque, hwaddr offset,
                                     uint64_t value, unsigned size)
{
    IPodClassicTimerState *s = opaque;
    unsigned ch;
    hwaddr reg;

    if (offset == S5L8702_TIMER_TSTAT) {
        /* Write-one-to-clear, which is how Rockbox acknowledges timer F. */
        s->tstat &= ~(uint32_t)value;
        ipod_classic_timer_update_irq(s);
        return;
    }

    if (!decode_offset(offset, &ch, &reg)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad offset 0x%" HWADDR_PRIx
                      " (value 0x%" PRIx64 ")\n", __func__, offset, value);
        return;
    }

    IPodClassicTimerChannel *c = &s->chan[ch];

    switch (reg) {
    case S5L8702_TIMER_CON: {
        /*
         * On the 16-bit timers the status bits live in this register and are
         * cleared by writing them back, which is what "TBCON = TBCON" in
         * Rockbox's tick handler does. Keep any status bit the guest did not
         * write a one to.
         */
        uint32_t status_mask = S5L8702_TIMER_CON_INT_MASK <<
                               S5L8702_TIMER_CON_INT_STAT_SHIFT;
        uint32_t kept = c->con & status_mask & ~(uint32_t)value;

        c->con = ((uint32_t)value & ~status_mask) | (chan_is_32bit(c) ? 0 : kept);
        chan_reload(c);
        ipod_classic_timer_update_irq(s);
        break;
    }
    case S5L8702_TIMER_CMD:
        c->cmd = value;
        if (value & S5L8702_TIMER_CMD_CLR) {
            /* Initialise: reset the counter and latch DATA0/DATA1. */
            chan_reload(c);
        }
        ptimer_transaction_begin(c->ptimer);
        if (value & S5L8702_TIMER_CMD_EN) {
            ptimer_run(c->ptimer, 0);
        } else {
            ptimer_stop(c->ptimer);
        }
        ptimer_transaction_commit(c->ptimer);
        break;
    case S5L8702_TIMER_DATA0:
        c->data0 = value;
        chan_reload(c);
        break;
    case S5L8702_TIMER_DATA1:
        c->data1 = value;
        break;
    case S5L8702_TIMER_PRE:
        c->pre = value;
        chan_reload(c);
        break;
    case S5L8702_TIMER_CNT:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: CNT of timer %c is read-only\n",
                      __func__, 'A' + ch);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad register 0x%" HWADDR_PRIx
                      " of timer %c\n", __func__, reg, 'A' + ch);
        break;
    }
}

static const MemoryRegionOps ipod_classic_timer_ops = {
    .read = ipod_classic_timer_read,
    .write = ipod_classic_timer_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void ipod_classic_timer_reset(DeviceState *dev)
{
    IPodClassicTimerState *s = IPOD_CLASSIC_TIMER(dev);

    s->tstat = 0;
    for (unsigned i = 0; i < S5L8702_NUM_TIMERS; i++) {
        IPodClassicTimerChannel *c = &s->chan[i];

        c->con = c->cmd = c->data0 = c->data1 = c->pre = 0;
        ptimer_transaction_begin(c->ptimer);
        ptimer_stop(c->ptimer);
        ptimer_transaction_commit(c->ptimer);
    }
    ipod_classic_timer_update_irq(s);
}

static void ipod_classic_timer_init(Object *obj)
{
    IPodClassicTimerState *s = IPOD_CLASSIC_TIMER(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &ipod_classic_timer_ops, s,
                          TYPE_IPOD_CLASSIC_TIMER, S5L8702_TIMER_MEM_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);

    sysbus_init_irq(sbd, &s->irq16);
    sysbus_init_irq(sbd, &s->irq32);

    for (unsigned i = 0; i < S5L8702_NUM_TIMERS; i++) {
        s->chan[i].parent = s;
        s->chan[i].index = i;
        s->chan[i].ptimer = ptimer_init(ipod_classic_timer_tick, &s->chan[i],
                                        PTIMER_POLICY_LEGACY);
    }
}

static const VMStateDescription vmstate_ipod_classic_timer_channel = {
    .name = "ipod_classic_timer_channel",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_PTIMER(ptimer, IPodClassicTimerChannel),
        VMSTATE_UINT32(con, IPodClassicTimerChannel),
        VMSTATE_UINT32(cmd, IPodClassicTimerChannel),
        VMSTATE_UINT32(data0, IPodClassicTimerChannel),
        VMSTATE_UINT32(data1, IPodClassicTimerChannel),
        VMSTATE_UINT32(pre, IPodClassicTimerChannel),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_ipod_classic_timer = {
    .name = TYPE_IPOD_CLASSIC_TIMER,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(chan, IPodClassicTimerState, S5L8702_NUM_TIMERS,
                             1, vmstate_ipod_classic_timer_channel,
                             IPodClassicTimerChannel),
        VMSTATE_UINT32(tstat, IPodClassicTimerState),
        VMSTATE_END_OF_LIST()
    }
};

static void ipod_classic_timer_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = ipod_classic_timer_reset;
    dc->vmsd = &vmstate_ipod_classic_timer;
    dc->desc = "S5L8702 timers";
}

static const TypeInfo ipod_classic_timer_info = {
    .name          = TYPE_IPOD_CLASSIC_TIMER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IPodClassicTimerState),
    .instance_init = ipod_classic_timer_init,
    .class_init    = ipod_classic_timer_class_init,
};

static void ipod_classic_timer_register_types(void)
{
    type_register_static(&ipod_classic_timer_info);
}

type_init(ipod_classic_timer_register_types)
