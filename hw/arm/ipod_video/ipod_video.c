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
#include "hw/sysbus.h"

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
 * The timer block. Only USEC_TIMER is modelled so far, which is the one the
 * firmware cannot get past: it is a free-running microsecond counter and every
 * busy-wait in Rockbox is written against it, so a register stuck at zero
 * turns the first delay loop into an infinite one.
 *
 * TIMER1 and TIMER2 are the programmable ones that raise IRQ 0 and IRQ 1.
 * They are read and written here but do not count or interrupt yet; there is
 * no interrupt controller to raise anything through.
 */
#define PP_TIMER1_CFG   0x00
#define PP_TIMER1_VAL   0x04
#define PP_TIMER2_CFG   0x08
#define PP_TIMER2_VAL   0x0c
#define PP_USEC_TIMER   0x10

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
    case PP_TIMER2_VAL:
        qemu_log_mask(LOG_UNIMP, "%s: TIMER%d_VAL is not counting yet\n",
                      __func__, offset == PP_TIMER1_VAL ? 1 : 2);
        return 0;
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
        break;
    case PP_TIMER2_CFG:
        nms->timer2_cfg = value;
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

static void allocate_ram(MemoryRegion *top, const char *name, uint32_t addr,
                         uint32_t size)
{
    MemoryRegion *sec = g_new(MemoryRegion, 1);
    memory_region_init_ram(sec, NULL, name, size, &error_fatal);
    memory_region_add_subregion(top, addr, sec);
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

static void ipod_video_instance_init(Object *obj)
{
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
    { "pp502x.intc",      INTC_MEM_BASE,      0x1000 },
    { "pp502x.clock",     CLOCK_MEM_BASE,     0x1000 },
    { "pp502x.dev-ctl",   DEV_CTL_MEM_BASE,   0x1000 },
    { "pp502x.dma",       DMA_MEM_BASE,       0x1000 },
    { "pp502x.cache-ctl", CACHE_CTL_MEM_BASE, 0x1000 },
    { "pp502x.gpio",      GPIO_MEM_BASE,      0x1000 },
    { "pp502x.i2s",       I2S_MEM_BASE,       0x0400 },
    { "pp502x.lcd",       LCD_MEM_BASE,       0x1000 },
    { "pp502x.i2c",       I2C_MEM_BASE,       0x1000 },
    { "pp502x.ide",       IDE_MEM_BASE,       0x1000 },
    { "pp502x.usb",       USB_MEM_BASE,       0x1000 },
    { "pp502x.mmap",      MMAP_MEM_BASE,      0x0040 },
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

    allocate_ram(sysmem, "dram", DRAM_MEM_BASE, DRAM_MEM_SIZE);
    allocate_ram(sysmem, "iram", IRAM_MEM_BASE, IRAM_MEM_SIZE);

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
    memory_region_add_subregion(sysmem, PROC_ID_MEM_BASE, proc_id);

    MemoryRegion *proc_ctl = g_new(MemoryRegion, 1);
    memory_region_init_io(proc_ctl, NULL, &pp502x_proc_ctl_ops, nms,
                          "pp502x.proc-ctl", 0x1000);
    memory_region_add_subregion(sysmem, PROC_CTL_MEM_BASE, proc_ctl);

    MemoryRegion *timer = g_new(MemoryRegion, 1);
    memory_region_init_io(timer, NULL, &pp502x_timer_ops, nms,
                          "pp502x.timer", 0x1000);
    memory_region_add_subregion(sysmem, TIMER_MEM_BASE, timer);

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
