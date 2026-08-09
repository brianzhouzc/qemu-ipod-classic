/*
 * Apple iPod 5th generation ("Video"), PortalPlayer PP5020/PP5022.
 *
 * First milestone: enough of the SoC to load the osos image the way the ROM
 * loader does and start executing it, with a working serial console. Every
 * peripheral we know the address of but have not modelled is registered as an
 * unimplemented device, so -d unimp reports what the firmware reaches for
 * next.
 *
 * Not modelled yet, and worth knowing before reading anything into a run:
 *
 *   - The second core. The PP502x has two ARM7TDMI cores, CPU and COP,
 *     controlled through CPU_CTL/COP_CTL at 0x60007000. Only one is created
 *     here, so firmware that waits on the other will hang.
 *   - The remap unit at 0xf000f000. Rockbox's application aliases DRAM to
 *     address 0 through it; the bootloader stage runs from DRAM_START and does
 *     not need it.
 *
 * See wiki/07-pp502x-hardware.md.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/arm/boot.h"
#include "hw/loader.h"
#include "elf.h"
#include "exec/address-spaces.h"
#include "hw/misc/unimp.h"
#include "hw/irq.h"
#include "hw/char/serial.h"
#include "sysemu/sysemu.h"
#include "sysemu/reset.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "hw/arm/ipod_video/ipod_video.h"
#include "hw/arm/ipod_video/ipod_video_bcm.h"
#include "hw/arm/ipod_video/ipod_video_intc.h"
#include "hw/sysbus.h"
#include "hw/arm/ipod_video/ipod_video_ide.h"
#include "sysemu/blockdev.h"
#include "hw/qdev-properties.h"
#include "ui/console.h"

/*
 * PROCESSOR_ID, the very first thing the firmware touches. Both cores run the
 * same image out of the same memory and tell themselves apart by reading this
 * byte: 0x55 means "you are the CPU", 0xaa means "you are the COP". Rockbox's
 * crt0-pp.S branches on it before doing anything else, so it cannot be left as
 * an unimplemented stub reading back zero -- that is neither value, and the
 * firmware wanders off.
 *
 * With only one core modelled it always answers CPU.
 */
static uint64_t pp502x_proc_id_read(void *opaque, hwaddr offset, unsigned size)
{
    return offset == 0 ? PP_PROC_ID_CPU : 0;
}

static void pp502x_proc_id_write(void *opaque, hwaddr offset, uint64_t value,
                                 unsigned size)
{
    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: PROCESSOR_ID is read-only (wrote 0x%" PRIx64
                  " at +0x%" HWADDR_PRIx ")\n", __func__, value, offset);
}

static const MemoryRegionOps pp502x_proc_id_ops = {
    .read = pp502x_proc_id_read,
    .write = pp502x_proc_id_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 1,
    .impl.max_access_size = 4,
};

/*
 * CPU_CTL and COP_CTL, the two core-control registers.
 *
 * The firmware's first act after identifying itself as the CPU is to wait for
 * the other core to go to sleep, from crt0-pp-bl.S:
 *
 *     cpu:
 *         ldr  r0, =COP_STATUS
 *     1:  ldr  r1, [r0]
 *         tst  r1, #COPSLEEPING
 *         beq  1b
 *
 * We model one core, so the COP is asleep and always will be. Reporting that
 * is the honest answer rather than a workaround: a core that never runs is
 * exactly a core that is permanently sleeping. What we cannot do yet is run
 * anything the firmware hands to the COP, so anything that offloads work to it
 * will stall.
 */
static uint64_t pp502x_proc_ctl_read(void *opaque, hwaddr offset, unsigned size)
{
    IPodVideoMachineState *nms = opaque;

    switch (offset) {
    case PP_PROC_CTL_CPU:
        return nms->cpu_ctl;
    case PP_PROC_CTL_COP:
        return nms->cop_ctl | PP_PROC_SLEEPING;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unknown register +0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }
}

static void pp502x_proc_ctl_write(void *opaque, hwaddr offset, uint64_t value,
                                  unsigned size)
{
    IPodVideoMachineState *nms = opaque;

    switch (offset) {
    case PP_PROC_CTL_CPU:
        nms->cpu_ctl = value;
        if (value & PP_PROC_SLEEPING) {
            qemu_log_mask(LOG_UNIMP, "%s: the CPU asked to sleep, which is not "
                          "modelled; it will keep running\n", __func__);
        }
        break;
    case PP_PROC_CTL_COP:
        /* Waking a core that does not exist has no effect. */
        nms->cop_ctl = value;
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unknown register +0x%" HWADDR_PRIx
                      " (wrote 0x%" PRIx64 ")\n", __func__, offset, value);
        break;
    }
}

static const MemoryRegionOps pp502x_proc_ctl_ops = {
    .read = pp502x_proc_ctl_read,
    .write = pp502x_proc_ctl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

/*
 * The timer block.
 *
 * USEC_TIMER is a free-running microsecond counter that every busy-wait in
 * Rockbox is written against. TIMER1 and TIMER2 are programmable and raise
 * IRQ 0 and IRQ 1; TIMER1 is what drives current_tick, which the cooperative
 * scheduler and every timeout in the firmware depend on. From kernel-pp.c:
 *
 *     TIMER1_CFG = 0xc0000000 | (interval_in_ms*1000 - 1);
 *     TIMER1_VAL;   -- read to acknowledge
 *
 * So bit 31 enables the timer, bit 30 enables its interrupt, the low bits are
 * the period in microseconds minus one, and reading VAL acknowledges.
 */
#define PP_TIMER_CFG_ENABLE     0x80000000
#define PP_TIMER_CFG_IRQ_ENABLE 0x40000000
#define PP_TIMER_CFG_PERIOD     0x0fffffff
#define PP_TIMER1_CFG   0x00
#define PP_TIMER1_VAL   0x04
#define PP_TIMER2_CFG   0x08
#define PP_TIMER2_VAL   0x0c
#define PP_USEC_TIMER   0x10

static void pp502x_timer_rearm(IPodVideoMachineState *nms, int n)
{
    uint32_t cfg = n ? nms->timer2_cfg : nms->timer1_cfg;
    QEMUTimer *t = n ? nms->timer2 : nms->timer1;

    if (!(cfg & PP_TIMER_CFG_ENABLE)) {
        timer_del(t);
        return;
    }
    /* The period is stored one less than it is, and counts microseconds. */
    timer_mod(t, qemu_clock_get_us(QEMU_CLOCK_VIRTUAL) +
                 (cfg & PP_TIMER_CFG_PERIOD) + 1);
}

static void pp502x_timer_expired(IPodVideoMachineState *nms, int n)
{
    uint32_t cfg = n ? nms->timer2_cfg : nms->timer1_cfg;

    if (cfg & PP_TIMER_CFG_IRQ_ENABLE) {
        nms->timer_pending[n] = true;
        qemu_set_irq(qdev_get_gpio_in(nms->intc, n), 1);
    }
    pp502x_timer_rearm(nms, n);
}

static void pp502x_timer1_expired(void *opaque)
{
    pp502x_timer_expired(opaque, 0);
}

static void pp502x_timer2_expired(void *opaque)
{
    pp502x_timer_expired(opaque, 1);
}

static uint64_t pp502x_timer_read(void *opaque, hwaddr offset, unsigned size)
{
    IPodVideoMachineState *nms = opaque;

    switch (offset) {
    case PP_USEC_TIMER:
        /* TIMER_FREQ in pp5020.h is 1000000, so this counts microseconds. */
        return qemu_clock_get_us(QEMU_CLOCK_VIRTUAL);
    case PP_TIMER1_CFG:
        return nms->timer1_cfg;
    case PP_TIMER2_CFG:
        return nms->timer2_cfg;
    case PP_TIMER1_VAL:
    case PP_TIMER2_VAL: {
        /* Reading the value is how the firmware acknowledges the interrupt. */
        int n = (offset == PP_TIMER1_VAL) ? 0 : 1;

        nms->timer_pending[n] = false;
        qemu_set_irq(qdev_get_gpio_in(nms->intc, n), 0);
        return 0;
    }
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unknown register +0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }
}

static void pp502x_timer_write(void *opaque, hwaddr offset, uint64_t value,
                               unsigned size)
{
    IPodVideoMachineState *nms = opaque;

    switch (offset) {
    case PP_TIMER1_CFG:
        nms->timer1_cfg = value;
        pp502x_timer_rearm(nms, 0);
        break;
    case PP_TIMER2_CFG:
        nms->timer2_cfg = value;
        pp502x_timer_rearm(nms, 1);
        break;
    case PP_USEC_TIMER:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: USEC_TIMER is read-only\n",
                      __func__);
        break;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: unknown register +0x%" HWADDR_PRIx
                      " (wrote 0x%" PRIx64 ")\n", __func__, offset, value);
        break;
    }
}

static const MemoryRegionOps pp502x_timer_ops = {
    .read = pp502x_timer_read,
    .write = pp502x_timer_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

/*
 * The cache controller, which is also where the vector table lives or does
 * not. From pp5020.h:
 *
 *     #define CACHE_CTL_ENABLE     0x0001
 *     #define CACHE_CTL_RUN        0x0002
 *     #define CACHE_CTL_INIT       0x0004
 *     #define CACHE_CTL_VECT_REMAP 0x0010
 *     #define CACHE_CTL_READY      0x4000
 *
 * VECT_REMAP is the one that matters. The ARM takes its exceptions from
 * address 0, and on this SoC that address is the flash ROM until the firmware
 * asks for the vectors to be remapped into IRAM, where it has installed its
 * own. Leave the bit unimplemented and every exception vectors into blank
 * space -- which is exactly what was happening: the CPU walked forward through
 * a window of zeroes, one NOP at a time, until it fell out of the map.
 *
 * The cache itself is not modelled. There is nothing to make coherent here,
 * so enabling it only has to be acknowledged.
 */
#define PP_CACHE_CTL            0x00
#define PP_CACHE_CTL_VECT_REMAP 0x0010
#define PP_CACHE_CTL_READY      0x4000

static uint64_t pp502x_cache_ctl_read(void *opaque, hwaddr offset, unsigned size)
{
    IPodVideoMachineState *nms = opaque;

    if (offset == PP_CACHE_CTL) {
        /* Always ready, never busy: there is no cache to wait for. */
        return nms->cache_ctl | PP_CACHE_CTL_READY;
    }
    return 0;
}

static void pp502x_cache_ctl_write(void *opaque, hwaddr offset, uint64_t value,
                                   unsigned size)
{
    IPodVideoMachineState *nms = opaque;

    if (offset != PP_CACHE_CTL) {
        return;
    }
    nms->cache_ctl = value;
    memory_region_set_enabled(nms->vect_remap,
                              !!(value & PP_CACHE_CTL_VECT_REMAP));
}

static const MemoryRegionOps pp502x_cache_ctl_ops = {
    .read = pp502x_cache_ctl_read,
    .write = pp502x_cache_ctl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

/*
 * The remap unit, MMAP0..7 at 0xf000f000.
 *
 * This is how low addresses stop being the flash ROM and become real memory.
 * Apple's firmware is linked to run from 0: tracing the boot shows the Rockbox
 * bootloader hand control to it at DRAM_START, its reset path run through
 * 0x100001f0..0x10000234, and then jump straight to 0x23c -- an absolute low
 * address that only makes sense once DRAM is mapped there. Without the remap
 * that jump lands in the blank flash window and the CPU NOP-sleds upward until
 * it falls out of the map.
 *
 * Modelled coarsely: programming MMAP0 at all switches DRAM in at address 0.
 * The registers encode a logical base, a physical base and masks, none of
 * which is decoded here, so a guest asking for anything other than the usual
 * whole-of-DRAM-at-zero mapping will not get what it asked for.
 */
static uint64_t pp502x_mmap_read(void *opaque, hwaddr offset, unsigned size)
{
    IPodVideoMachineState *nms = opaque;

    return offset < sizeof(nms->mmap_regs) ? nms->mmap_regs[offset / 4] : 0;
}

static void pp502x_mmap_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size)
{
    IPodVideoMachineState *nms = opaque;

    if (offset >= sizeof(nms->mmap_regs)) {
        return;
    }
    nms->mmap_regs[offset / 4] = value;

    /* MMAP0 is the pair at +0x00 and +0x04. */
    if (offset <= 0x04 && value != 0 && !nms->dram_remapped) {
        nms->dram_remapped = true;
        memory_region_set_enabled(nms->dram_remap, true);
    }
}

static const MemoryRegionOps pp502x_mmap_ops = {
    .read = pp502x_mmap_read,
    .write = pp502x_mmap_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

/*
 * GPIO, enough of it to answer the buttons.
 *
 * The registers go four ports to a group: ENABLE at +0x00, OUTPUT_EN at +0x10,
 * OUTPUT_VAL at +0x20 and INPUT_VAL at +0x30. Only the input values matter
 * here, and they are active low -- from Rockbox's button-clickwheel.c:
 *
 *     bool button_hold(void) { return (GPIOA_INPUT_VAL & 0x20) ? false : true; }
 *
 * Reading back zero therefore means every button is held, including hold,
 * which is why the Rockbox bootloader was taking its "boot Apple's firmware"
 * path instead of loading rockbox.ipod. Inputs read as all ones: nothing
 * pressed.
 */
#define PP_GPIO_INPUT_VAL   0x30
#define PP_GPIO_GROUP_SIZE  0x40

static uint64_t pp502x_gpio_read(void *opaque, hwaddr offset, unsigned size)
{
    IPodVideoMachineState *nms = opaque;
    hwaddr reg = offset % PP_GPIO_GROUP_SIZE;

    if (reg >= PP_GPIO_INPUT_VAL && reg < PP_GPIO_INPUT_VAL + 0x10) {
        return 0xffffffff;
    }
    return offset < sizeof(nms->gpio_regs) ? nms->gpio_regs[offset / 4] : 0;
}

static void pp502x_gpio_write(void *opaque, hwaddr offset, uint64_t value,
                              unsigned size)
{
    IPodVideoMachineState *nms = opaque;

    if (offset < sizeof(nms->gpio_regs)) {
        nms->gpio_regs[offset / 4] = value;
    }
}

static const MemoryRegionOps pp502x_gpio_ops = {
    .read = pp502x_gpio_read,
    .write = pp502x_gpio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

/*
 * The click wheel.
 *
 * Rockbox decodes it in button-clickwheel.c, which pins the format down
 * exactly:
 *
 *     unsigned status = CLICKWHEEL_DATA;
 *     if ((status & 0x800000ff) == 0x8000001a) {
 *         if (status & 0x00000100) btn |= BUTTON_SELECT;
 *         if (status & 0x00000200) btn |= BUTTON_RIGHT;
 *         if (status & 0x00000400) btn |= BUTTON_LEFT;
 *         if (status & 0x00000800) btn |= BUTTON_PLAY;
 *         if (status & 0x00001000) btn |= BUTTON_MENU;
 *
 * So a valid packet is 0x8000001a and the buttons are single bits on top of
 * it. Reading back zero is not "no buttons", it is "no packet at all", which
 * is not the same thing and is part of why the firmware would not proceed.
 *
 * The wheel itself -- bit 30 with a position in bits 16..22 -- is not
 * generated yet; only the five buttons are, from the keyboard.
 *
 * On the PP the wheel is interrupt driven, not polled: ipod_4g_button_read()
 * is only ever called from ipod_4g_button_int(), and it refuses to look at the
 * data at all unless a status bit is set first:
 *
 *     if ((inl(0x7000c104) & 0x04000000) != 0) {
 *         unsigned status = CLICKWHEEL_DATA;
 *         ...
 *     }
 *
 * and the handler acknowledges with outl(inl(0x7000c104) | 0x0c000000, ...).
 * So a key press has to do two things: latch that bit, and raise the wheel's
 * interrupt, which on this SoC is the I2C line in the high bank. Without both
 * the firmware never reads the wheel and nothing at all happens.
 */
#define PP_CLICKWHEEL_PACKET    0x8000001a
#define PP_WHEEL_STATUS_OFF     0x104
#define PP_WHEEL_STATUS_RX      (1 << 26)
#define PP_WHEEL_TOUCHED        0x40000000
#define PP_WHEEL_POS_SHIFT      16
#define PP_WHEEL_POS_MASK       0x7f
/*
 * "Highest wheel = 0x5F, clockwise increases" -- 96 positions to the turn,
 * which is WHEELCLICKS_PER_ROTATION. Rockbox needs WHEEL_SENSITIVITY (4)
 * clicks of accumulated travel before it moves the selection by one, so each
 * key event steps two and a press/release pair moves exactly one item.
 */
#define PP_WHEEL_CLICKS         96
#define PP_WHEEL_STEP           2
#define PP_WHEEL_SELECT         0x00000100
#define PP_WHEEL_RIGHT          0x00000200
#define PP_WHEEL_LEFT           0x00000400
#define PP_WHEEL_PLAY           0x00000800
#define PP_WHEEL_MENU           0x00001000

static uint64_t pp502x_clickwheel_read(void *opaque, hwaddr offset,
                                       unsigned size)
{
    IPodVideoMachineState *nms = opaque;

    if (offset == CLICKWHEEL_DATA_OFF) {
        uint32_t packet = PP_CLICKWHEEL_PACKET | nms->wheel_buttons;
        if (nms->wheel_touched) {
            packet |= PP_WHEEL_TOUCHED
                   | ((nms->wheel_position & PP_WHEEL_POS_MASK)
                      << PP_WHEEL_POS_SHIFT);
        }
        return packet;
    }
    if (offset == PP_WHEEL_STATUS_OFF) {
        return nms->wheel_regs[offset / 4]
             | (nms->wheel_pending ? PP_WHEEL_STATUS_RX : 0);
    }
    return offset < sizeof(nms->wheel_regs) ? nms->wheel_regs[offset / 4] : 0;
}

static void pp502x_clickwheel_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned size)
{
    IPodVideoMachineState *nms = opaque;

    if (offset == PP_WHEEL_STATUS_OFF && (value & PP_WHEEL_STATUS_RX)) {
        /* The handler acknowledges the packet by writing the bit back. */
        nms->wheel_pending = false;
        qemu_set_irq(qdev_get_gpio_in(nms->intc, PP_I2C_IRQ), 0);
        value &= ~PP_WHEEL_STATUS_RX;
    }

    if (offset < sizeof(nms->wheel_regs)) {
        nms->wheel_regs[offset / 4] = value;
    }
}

static const MemoryRegionOps pp502x_clickwheel_ops = {
    .read = pp502x_clickwheel_read,
    .write = pp502x_clickwheel_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

/* w/a/s/d around the wheel, enter for select. */
static void ipod_video_key_event(void *opaque, int keycode)
{
    IPodVideoMachineState *nms = opaque;
    bool released = keycode & 0x80;
    uint32_t bit;

    switch (keycode & 0x7f) {
    case 0x11: bit = PP_WHEEL_MENU;   break;   /* w */
    case 0x1e: bit = PP_WHEEL_LEFT;   break;   /* a */
    case 0x1f: bit = PP_WHEEL_PLAY;   break;   /* s */
    case 0x20: bit = PP_WHEEL_RIGHT;  break;   /* d */
    case 0x1c: bit = PP_WHEEL_SELECT; break;   /* enter */

    /*
     * Turning the wheel. Both the press and the release step it, so one
     * keystroke is four clicks: enough for Rockbox to call it a scroll, and
     * frequent enough that its 150 ms untouch timeout never fires and resets
     * the accumulator.
     */
    case 0x12:                                 /* e: clockwise */
    case 0x10:                                 /* q: anticlockwise */
        nms->wheel_position += (keycode & 0x7f) == 0x12
                             ? PP_WHEEL_STEP
                             : PP_WHEEL_CLICKS - PP_WHEEL_STEP;
        nms->wheel_position %= PP_WHEEL_CLICKS;
        nms->wheel_touched = true;
        nms->wheel_pending = true;
        qemu_set_irq(qdev_get_gpio_in(nms->intc, PP_I2C_IRQ), 1);
        return;

    default: return;
    }

    if (released) {
        nms->wheel_buttons &= ~bit;
    } else {
        nms->wheel_buttons |= bit;
    }

    /* A change is a new packet: latch the status bit and interrupt. */
    nms->wheel_pending = true;
    qemu_set_irq(qdev_get_gpio_in(nms->intc, PP_I2C_IRQ), 1);
}

/*
 * Clock and power control.
 *
 * Only PLL_STATUS has behaviour, and it is the one the firmware cannot get
 * past. From system-pp502x.c, raising the clock on a PP5022:
 *
 *     PLL_CONTROL = 0x8a121403;
 *     while (!(PLL_STATUS & 0x80000000));   -- wait for relock
 *
 * Bit 31 says the PLL has locked. There is no PLL here to take time settling,
 * so it is locked as soon as it is asked for. Everything else in the block is
 * stored and handed back.
 */
#define PP_PLL_STATUS       0x3c
#define PP_PLL_LOCKED       0x80000000

static uint64_t pp502x_clock_read(void *opaque, hwaddr offset, unsigned size)
{
    IPodVideoMachineState *nms = opaque;

    if (offset == PP_PLL_STATUS) {
        return nms->clock_regs[offset / 4] | PP_PLL_LOCKED;
    }
    return offset < sizeof(nms->clock_regs) ? nms->clock_regs[offset / 4] : 0;
}

static void pp502x_clock_write(void *opaque, hwaddr offset, uint64_t value,
                               unsigned size)
{
    IPodVideoMachineState *nms = opaque;

    if (offset < sizeof(nms->clock_regs)) {
        nms->clock_regs[offset / 4] = value;
    }
}

static const MemoryRegionOps pp502x_clock_ops = {
    .read = pp502x_clock_read,
    .write = pp502x_clock_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

/*
 * Device control: chip version and straps, DEV_INIT1/2, the external memory
 * bus timings and the 32-bit GPO port. Rockbox's pp5020.h names most of it --
 * PP_VER1 at +0x00, DEV_INIT1 at +0x10, DEV_INIT2 at +0x20, DEV_TIMING1 at
 * +0x34, GPO32_VAL at +0x80, GPO32_ENABLE at +0x84 -- and everything named is
 * a plain register that reads back what was written.
 *
 * The exception is +0x28, which pp5020.h does not name at all but which the
 * USB bring-up in usb-fw-pp502x.c blocks on:
 *
 *     DEV_INIT2 |= INIT_USB;
 *     while ((inl(0x70000028) & 0x80) == 0);
 *     outl(inl(0x70000028) | 0x2, 0x70000028);
 *
 * Bit 7 is the USB block reporting that it came out of reset. Nothing here
 * takes time to power up, so it reads as set from the start; leaving it clear
 * is an unbreakable spin, and it was where the firmware sat once the cache
 * arrays stopped faulting.
 */
#define PP_DEV_USB_STATUS   0x28
#define PP_DEV_USB_READY    0x80
#define PP_GPO32_VAL        0x80
#define PP_GPO32_BCM_POWER  0x4000

static uint64_t pp502x_dev_ctl_read(void *opaque, hwaddr offset, unsigned size)
{
    IPodVideoMachineState *nms = opaque;

    if (offset >= sizeof(nms->dev_regs)) {
        return 0;
    }
    if (offset == PP_DEV_USB_STATUS) {
        return nms->dev_regs[offset / 4] | PP_DEV_USB_READY;
    }
    return nms->dev_regs[offset / 4];
}

static void pp502x_dev_ctl_write(void *opaque, hwaddr offset, uint64_t value,
                                 unsigned size)
{
    IPodVideoMachineState *nms = opaque;

    if (offset < sizeof(nms->dev_regs)) {
        nms->dev_regs[offset / 4] = value;
    }
}

static const MemoryRegionOps pp502x_dev_ctl_ops = {
    .read = pp502x_dev_ctl_read,
    .write = pp502x_dev_ctl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

static MemoryRegion *allocate_ram(MemoryRegion *top, const char *name,
                                  uint32_t addr, uint32_t size)
{
    MemoryRegion *sec = g_new(MemoryRegion, 1);
    memory_region_init_ram(sec, NULL, name, size, &error_fatal);
    memory_region_add_subregion(top, addr, sec);
    return sec;
}

static void ipod_video_cpu_reset(void *opaque)
{
    IPodVideoMachineState *nms = IPOD_VIDEO_MACHINE((MachineState *)opaque);
    CPUState *cs = CPU(nms->cpu);

    cpu_reset(cs);
    cpu_set_pc(cs, nms->reset_pc);
}

/*
 * The ROM loader reads the osos image off the firmware partition, copies it
 * whole to DRAM_START and jumps to DRAM_START + entryOffset. We do its job
 * directly: there is no point emulating the NOR ROM when all it does is copy
 * and jump, and we do not have a dump of it anyway.
 *
 * An ELF is honoured as an ELF, which is how a freshly built Rockbox
 * bootloader can be loaded without going through an osos image at all.
 */
static void ipod_video_load_image(MachineState *machine,
                                  IPodVideoMachineState *nms)
{
    uint64_t entry = 0, lowaddr = 0, highaddr = 0;
    ssize_t size;

    nms->reset_pc = DRAM_MEM_BASE + nms->entry_offset;

    if (!machine->kernel_filename) {
        return;
    }

    size = load_elf(machine->kernel_filename, NULL, NULL, NULL, &entry,
                    &lowaddr, &highaddr, NULL, 0, EM_ARM, 1, 0);
    if (size > 0) {
        info_report("loaded ELF \"%s\": 0x%" PRIx64 "-0x%" PRIx64
                    ", entry 0x%" PRIx64, machine->kernel_filename,
                    lowaddr, highaddr, entry);
        nms->reset_pc = entry;
        return;
    }

    size = load_image_targphys(machine->kernel_filename, DRAM_MEM_BASE,
                               DRAM_MEM_SIZE);
    if (size < 0) {
        error_report("could not load \"%s\" at 0x%x", machine->kernel_filename,
                     (unsigned)DRAM_MEM_BASE);
        exit(1);
    }
    info_report("loaded 0x%zx bytes at 0x%x, entry 0x%" HWADDR_PRIx,
                (size_t)size, (unsigned)DRAM_MEM_BASE, nms->reset_pc);
}

static void ipod_video_get_entry(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    IPodVideoMachineState *nms = IPOD_VIDEO_MACHINE(obj);
    uint32_t value = nms->entry_offset;

    visit_type_uint32(v, name, &value, errp);
}

static void ipod_video_set_entry(Object *obj, Visitor *v, const char *name,
                                 void *opaque, Error **errp)
{
    IPodVideoMachineState *nms = IPOD_VIDEO_MACHINE(obj);
    uint32_t value;

    if (!visit_type_uint32(v, name, &value, errp)) {
        return;
    }
    if (value >= DRAM_MEM_SIZE) {
        error_setg(errp, "entry offset 0x%x is past the end of DRAM", value);
        return;
    }
    nms->entry_offset = value;
}

static bool ipod_video_get_remap(Object *obj, Error **errp)
{
    return IPOD_VIDEO_MACHINE(obj)->force_remap;
}

static void ipod_video_set_remap(Object *obj, bool value, Error **errp)
{
    IPOD_VIDEO_MACHINE(obj)->force_remap = value;
}

static void ipod_video_instance_init(Object *obj)
{
    object_property_add_bool(obj, "remap", ipod_video_get_remap,
                             ipod_video_set_remap);
    object_property_set_description(obj, "remap",
        "Map DRAM at address 0 from reset, as the firmware would through the "
        "remap unit. Needed to run a Rockbox application image directly, since "
        "those are linked for address 0 and normally rely on the bootloader "
        "having set the mapping up first.");

    IPOD_VIDEO_MACHINE(obj)->hw_revision = IPOD_VIDEO_HW_REVISION;

    object_property_add(obj, "entry", "uint32", ipod_video_get_entry,
                        ipod_video_set_entry, NULL, NULL);
    object_property_set_description(obj, "entry",
        "Offset into the loaded image to start executing at, as the ROM "
        "loader would. An osos image with a Rockbox bootloader appended needs "
        "the entryOffset from the firmware partition directory; 0 runs "
        "Apple's firmware instead.");
}

/*
 * Blocks we know the address of but have not modelled. Registering them keeps
 * stray accesses out of the unassigned-memory path and makes -d unimp name the
 * block the firmware is talking to.
 */
static const struct { const char *name; hwaddr base; hwaddr size; }
ipod_video_unimplemented[] = {
    { "pp502x.usb",       USB_MEM_BASE,       0x1000 },
};

static void ipod_video_machine_init(MachineState *machine)
{
    IPodVideoMachineState *nms = IPOD_VIDEO_MACHINE(machine);
    MemoryRegion *sysmem = get_system_memory();
    Object *cpuobj;

    cpuobj = object_new(machine->cpu_type);
    nms->cpu = ARM_CPU(cpuobj);
    object_property_set_link(cpuobj, "memory", OBJECT(sysmem), &error_abort);
    object_property_set_bool(cpuobj, "realized", true, &error_fatal);
    nms->nsas = cpu_get_address_space(CPU(nms->cpu), ARMASIdx_NS);
    object_unref(cpuobj);

    MemoryRegion *dram = allocate_ram(sysmem, "dram", DRAM_MEM_BASE,
                                      DRAM_MEM_SIZE);
    MemoryRegion *iram = allocate_ram(sysmem, "iram", IRAM_MEM_BASE,
                                      IRAM_MEM_SIZE);

    /*
     * The PP's NOR flash sits at address 0 while the bootloader runs and is
     * remapped to 0x20000000 afterwards -- Rockbox's hwcompat.h says as much,
     * reading the hardware revision from 0x405c during boot and from
     * 0x2000405c later, with the comment "ROM is remapped".
     *
     * We have no dump of that flash, so this stands in for it: a blank window
     * carrying the one word the firmware actually asks for here. Without it
     * the read faults, the CPU vectors to the prefetch abort handler at 0xc,
     * finds nothing mapped there either, and spins on that forever.
     */
    MemoryRegion *rom = g_new(MemoryRegion, 1);
    memory_region_init_ram(rom, NULL, "flash-rom", FLASH_ROM_MEM_SIZE,
                           &error_fatal);
    memory_region_add_subregion(sysmem, FLASH_ROM_MEM_BASE, rom);

    /*
     * IRAM over the top of the flash window, for when the firmware asks for
     * the vectors to be remapped. Disabled until it does.
     */
    nms->vect_remap = g_new(MemoryRegion, 1);
    memory_region_init_alias(nms->vect_remap, NULL, "vector-remap",
                             iram, 0, IRAM_MEM_SIZE);
    memory_region_add_subregion_overlap(sysmem, 0, nms->vect_remap, 1);
    memory_region_set_enabled(nms->vect_remap, false);

    /* DRAM at address 0, switched in when the firmware programs MMAP0. */
    nms->dram_remap = g_new(MemoryRegion, 1);
    memory_region_init_alias(nms->dram_remap, NULL, "dram-remap", dram, 0,
                             DRAM_MEM_SIZE);
    memory_region_add_subregion_overlap(sysmem, 0, nms->dram_remap, 2);
    memory_region_set_enabled(nms->dram_remap, nms->force_remap);
    nms->dram_remapped = nms->force_remap;

    MemoryRegion *rom_alias = g_new(MemoryRegion, 1);
    memory_region_init_alias(rom_alias, NULL, "flash-rom-remapped", rom, 0,
                             FLASH_ROM_MEM_SIZE);
    memory_region_add_subregion(sysmem, FLASH_ROM_REMAP_BASE, rom_alias);

    uint32_t hwrev = cpu_to_le32(nms->hw_revision);
    cpu_physical_memory_write(FLASH_ROM_MEM_BASE + IPOD_HW_REVISION_OFF,
                              &hwrev, sizeof(hwrev));

    MemoryRegion *proc_id = g_new(MemoryRegion, 1);
    memory_region_init_io(proc_id, NULL, &pp502x_proc_id_ops, nms,
                          "pp502x.proc-id", 0x1000);
    memory_region_add_subregion_overlap(sysmem, PROC_ID_MEM_BASE, proc_id, 1);

    MemoryRegion *proc_ctl = g_new(MemoryRegion, 1);
    memory_region_init_io(proc_ctl, NULL, &pp502x_proc_ctl_ops, nms,
                          "pp502x.proc-ctl", 0x1000);
    memory_region_add_subregion_overlap(sysmem, PROC_CTL_MEM_BASE, proc_ctl, 1);

    /*
     * The interrupt controller, which everything below hangs off: without a
     * tick from TIMER1 the firmware's scheduler never runs and no timeout ever
     * expires.
     */
    nms->intc = qdev_new(TYPE_IPOD_VIDEO_INTC);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(nms->intc), &error_fatal);
    sysbus_mmio_map_overlap(SYS_BUS_DEVICE(nms->intc), 0, INTC_MEM_BASE, 1);
    sysbus_connect_irq(SYS_BUS_DEVICE(nms->intc), 0,
                       qdev_get_gpio_in(DEVICE(nms->cpu), ARM_CPU_IRQ));

    /*
     * The cache arrays. These are not registers, they are the cache's own
     * storage exposed on the bus, and PP5022 firmware writes them directly:
     * cache_invalidate_special() in system-pp502x.c walks the whole per-core
     * line-status array marking every line valid, as a workaround for lines
     * that would otherwise corrupt memory.
     *
     *     for (p = &CACHE_STATUS_BASE_CPU;
     *          p < (&CACHE_STATUS_BASE_CPU) + CACHE_SIZE/sizeof(*p);
     *          p += CACHEALIGN_SIZE/sizeof(*p))
     *         *p = CACHE_LINE_VALID | CACHE_ADDRESS_MASK;
     *
     * With nothing mapped there that first store is rejected outright. There
     * is no cache to model, so plain RAM is enough: the firmware only ever
     * reads back what it wrote.
     */
    allocate_ram(sysmem, "cache-arrays", CACHE_ARRAY_MEM_BASE,
                 CACHE_ARRAY_MEM_SIZE);

    MemoryRegion *mmap = g_new(MemoryRegion, 1);
    /*
     * The header documents MMAP0..7 as 0xf000f000..0xf000f03c, but the
     * firmware writes 0xf000f040 as well, so the block is bigger than the
     * eight register pairs Rockbox names. Cover a page.
     */
    memory_region_init_io(mmap, NULL, &pp502x_mmap_ops, nms,
                          "pp502x.mmap", 0x1000);
    memory_region_add_subregion(sysmem, MMAP_MEM_BASE, mmap);

    MemoryRegion *gpio = g_new(MemoryRegion, 1);
    memory_region_init_io(gpio, NULL, &pp502x_gpio_ops, nms,
                          "pp502x.gpio", 0x1000);
    memory_region_add_subregion_overlap(sysmem, GPIO_MEM_BASE, gpio, 1);

    MemoryRegion *wheel = g_new(MemoryRegion, 1);
    memory_region_init_io(wheel, NULL, &pp502x_clickwheel_ops, nms,
                          "pp502x.clickwheel", 0x1000);
    memory_region_add_subregion_overlap(sysmem, CLICKWHEEL_MEM_BASE, wheel, 1);
    qemu_add_kbd_event_handler(ipod_video_key_event, nms);

    MemoryRegion *clock = g_new(MemoryRegion, 1);
    memory_region_init_io(clock, NULL, &pp502x_clock_ops, nms,
                          "pp502x.clock", 0x1000);
    memory_region_add_subregion_overlap(sysmem, CLOCK_MEM_BASE, clock, 1);

    MemoryRegion *dev_ctl = g_new(MemoryRegion, 1);
    memory_region_init_io(dev_ctl, NULL, &pp502x_dev_ctl_ops, nms,
                          "pp502x.dev-ctl", 0x1000);
    memory_region_add_subregion_overlap(sysmem, DEV_CTL_MEM_BASE, dev_ctl, 1);

    /*
     * GPO32 bit 14 powers the Broadcom. Apple's flash ROM loader turns it on
     * before it hands over, and Rockbox reads it back to decide whether it has
     * to bring the chip up itself -- from lcd-video.c:
     *
     *     if (GPO32_VAL & 0x4000) {
     *         lcd_state.display_on = true;      -- BCM is powered
     *     } else {
     *         lcd_state.display_on = false;     -- must upload BCM firmware
     *         lcd_awake();
     *     }
     *
     * That firmware is the 'vmcs' section of the flash ROM, which we do not
     * have, so the second path leaves display_on false forever -- and
     * lcd_update_rect() returns early when it is, so every draw is dropped
     * without a word. The screen stayed black while the rest of Rockbox ran
     * perfectly happily. We skip the ROM loader, so its effect has to be
     * reproduced here.
     */
    nms->dev_regs[PP_GPO32_VAL / 4] = PP_GPO32_BCM_POWER;

    MemoryRegion *cache_ctl = g_new(MemoryRegion, 1);
    memory_region_init_io(cache_ctl, NULL, &pp502x_cache_ctl_ops, nms,
                          "pp502x.cache-ctl", 0x1000);
    memory_region_add_subregion_overlap(sysmem, CACHE_CTL_MEM_BASE, cache_ctl, 1);

    nms->timer1 = timer_new_us(QEMU_CLOCK_VIRTUAL, pp502x_timer1_expired, nms);
    nms->timer2 = timer_new_us(QEMU_CLOCK_VIRTUAL, pp502x_timer2_expired, nms);

    MemoryRegion *timer = g_new(MemoryRegion, 1);
    memory_region_init_io(timer, NULL, &pp502x_timer_ops, nms,
                          "pp502x.timer", 0x1000);
    memory_region_add_subregion_overlap(sysmem, TIMER_MEM_BASE, timer, 1);

    /*
     * A floor under the whole SoC register window. Rockbox's header documents
     * the blocks Rockbox uses, but Apple's firmware reaches for others -- the
     * first fault after the remap went in was a read of 0x60009000, which
     * appears nowhere in pp5020.h. An unknown register reading back zero is
     * survivable; an unmapped one is a data abort, and the handler it vectors
     * to faults in turn and never returns. The modelled blocks sit on top of
     * this at a higher priority.
     */
    create_unimplemented_device("pp502x.soc-regs", 0x60000000, 0x10000);
    create_unimplemented_device("pp502x.dev-regs", 0x70000000, 0x10000);

    for (size_t i = 0; i < ARRAY_SIZE(ipod_video_unimplemented); i++) {
        create_unimplemented_device(ipod_video_unimplemented[i].name,
                                    ipod_video_unimplemented[i].base,
                                    ipod_video_unimplemented[i].size);
    }

    /*
     * SER0 is a plain 16550 with its registers four bytes apart, so QEMU's own
     * model does the job. The interrupt is not wired yet because there is no
     * interrupt controller to wire it to; transmission does not depend on it.
     */
    serial_mm_init(sysmem, SER0_MEM_BASE, 2, NULL, 115200, serial_hd(0),
                   DEVICE_LITTLE_ENDIAN);

    /*
     * The panel hangs off a Broadcom companion chip rather than off the SoC,
     * so this is what actually puts pixels on screen.
     */
    DeviceState *bcm = qdev_new(TYPE_IPOD_VIDEO_BCM);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(bcm), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(bcm), 0, BCM_MEM_BASE);

    /*
     * The disk. The task file is a standard ATA one with the registers four
     * bytes apart at IDE_BASE + 0x1e0, and the control register at +0x3f8, so
     * QEMU's memory-mapped IDE covers it with shift 2. What is PP-specific is
     * the block of timing and configuration registers below 0x1e0, which is
     * left as an unimplemented stub: the firmware programs PIO and DMA
     * timings there, and none of that changes what a read returns.
     */
    /*
     * The disk. The task file is standard ATA with the registers four bytes
     * apart, but the PP wraps it in its own configuration, timing and DMA
     * registers, and the application needs that DMA: the Rockbox bootloader
     * reads with READ MULTIPLE, the app issues READ DMA. So the whole window
     * is one device rather than the generic mmio-ide with a stub beside it.
     */
    DeviceState *ide = qdev_new(TYPE_PP502X_IDE);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(ide), &error_fatal);
    sysbus_connect_irq(SYS_BUS_DEVICE(ide), 0,
                       qdev_get_gpio_in(nms->intc, PP_IDE_IRQ));
    sysbus_mmio_map(SYS_BUS_DEVICE(ide), 0, IDE_MEM_BASE);
    pp502x_ide_init_drive(ide, drive_get(IF_IDE, 0, 0));

    ipod_video_load_image(machine, nms);

    qemu_register_reset(ipod_video_cpu_reset, nms);
}

static void ipod_video_machine_class_init(ObjectClass *klass, void *data)
{
    MachineClass *mc = MACHINE_CLASS(klass);

    mc->desc = "Apple iPod 5th generation (Video), PortalPlayer PP502x";
    mc->init = ipod_video_machine_init;
    mc->max_cpus = 1;
    /*
     * The real cores are ARM7TDMI, which is ARMv4T, and QEMU has no model for
     * it. arm926 is ARMv5TE: a superset, so v4T firmware runs unchanged, and
     * it is the best exercised ARM model in the tree. -cpu ti925t gets the
     * architecture level exactly right if that ever turns out to matter.
     */
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm926");
}

static const TypeInfo ipod_video_machine_info = {
    .name          = TYPE_IPOD_VIDEO_MACHINE,
    .parent        = TYPE_MACHINE,
    .instance_size = sizeof(IPodVideoMachineState),
    .class_size    = sizeof(IPodVideoMachineClass),
    .class_init    = ipod_video_machine_class_init,
    .instance_init = ipod_video_instance_init,
};

static void ipod_video_machine_types(void)
{
    type_register_static(&ipod_video_machine_info);
}

type_init(ipod_video_machine_types)
