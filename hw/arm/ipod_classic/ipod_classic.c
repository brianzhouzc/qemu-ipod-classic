/*
 * Apple iPod classic (6th generation), Samsung S5L8702 SoC.
 *
 * Milestone H1: CPU, RAM and the interrupt controllers, plus a -kernel style
 * boot path so a raw image (a Rockbox build, for now) can be dropped straight
 * into SDRAM without going through a bootrom we do not have. Every peripheral
 * that is not modelled yet is registered as an unimplemented device so that
 * -d unimp tells us exactly what the guest reaches for next.
 *
 * See wiki/02-s5l8702-hardware.md for where the addresses come from and
 * wiki/04-roadmap.md for what comes next.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/arm/boot.h"
#include "hw/loader.h"
#include "elf.h"
#include "exec/address-spaces.h"
#include "hw/misc/unimp.h"
#include "hw/irq.h"
#include "sysemu/sysemu.h"
#include "sysemu/reset.h"
#include "hw/qdev-clock.h"
#include "hw/arm/exynos4210.h"
#include "hw/arm/ipod_classic/ipod_classic.h"
#include "hw/arm/ipod_classic/ipod_classic_timer.h"
#include "hw/sysbus.h"
#include "target/arm/cpregs.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "semihosting/semihost.h"

static MemoryRegion *allocate_ram(MemoryRegion *top, const char *name,
                                  uint32_t addr, uint32_t size)
{
    MemoryRegion *sec = g_new(MemoryRegion, 1);
    memory_region_init_ram(sec, NULL, name, size, &error_fatal);
    memory_region_add_subregion(top, addr, sec);
    return sec;
}

static void ipod_classic_cpu_setup(MachineState *machine, MemoryRegion **sysmem,
                                   ARMCPU **cpu, AddressSpace **nsas)
{
    Object *cpuobj = object_new(machine->cpu_type);
    *cpu = ARM_CPU(cpuobj);
    CPUState *cs = CPU(*cpu);

    *sysmem = get_system_memory();

    object_property_set_link(cpuobj, "memory", OBJECT(*sysmem), &error_abort);
    object_property_set_bool(cpuobj, "realized", true, &error_fatal);

    *nsas = cpu_get_address_space(cs, ARMASIdx_NS);

    object_unref(cpuobj);
}

/*
 * Reproduce what the bootrom leaves in the top of IRAM1 before it jumps into
 * the OS image. See the S5L8702_SYSINFO_* comments in the machine header for
 * the disassembly each field comes from.
 *
 * Written on every reset, not just at startup, because it lives in RAM the
 * guest is free to overwrite and because a guest-requested reboot (SWRCON)
 * has to come back to a machine that looks freshly powered on.
 */
static void ipod_classic_write_bootinfo(IPodClassicMachineState *nms)
{
    uint32_t sysinfo[S5L8702_SYSINFO_SIZE / 4] = {
        [S5L8702_SYSINFO_HW_ID_OFF / 4]     = cpu_to_le32(S5L8702_HW_ID),
        [S5L8702_SYSINFO_DRAM_SIZE_OFF / 4] = cpu_to_le32(SDRAM_MEM_SIZE),
        [S5L8702_SYSINFO_DRAM_BASE_OFF / 4] = cpu_to_le32(SDRAM_MEM_BASE),
    };
    uint32_t tag = cpu_to_le32(S5L8702_SYSINFO_MAGIC);
    uint32_t ptr = cpu_to_le32(S5L8702_SYSINFO_BASE);
    uint8_t zero[0x100] = { 0 };

    cpu_physical_memory_write(S5L8702_BOOTINFO_BASE, zero, sizeof(zero));
    cpu_physical_memory_write(S5L8702_SYSINFO_BASE, sysinfo, sizeof(sysinfo));
    cpu_physical_memory_write(S5L8702_BOOTINFO_TAG, &tag, sizeof(tag));
    cpu_physical_memory_write(S5L8702_BOOTINFO_PTR, &ptr, sizeof(ptr));
}

static void ipod_classic_cpu_reset(void *opaque)
{
    IPodClassicMachineState *nms = IPOD_CLASSIC_MACHINE((MachineState *)opaque);
    CPUState *cs = CPU(nms->cpu);

    cpu_reset(cs);
    cpu_set_pc(cs, nms->reset_pc);
    ipod_classic_write_bootinfo(nms);
}

static void ipod_classic_memory_setup(MachineState *machine,
                                      MemoryRegion *sysmem, AddressSpace *nsas)
{
    IPodClassicMachineState *nms = IPOD_CLASSIC_MACHINE(machine);

    MemoryRegion *iram0;

    allocate_ram(sysmem, "sdram", SDRAM_MEM_BASE, SDRAM_MEM_SIZE);
    iram0 = allocate_ram(sysmem, "iram0", IRAM0_MEM_BASE, IRAM0_MEM_SIZE);
    allocate_ram(sysmem, "iram1", IRAM1_MEM_BASE, IRAM1_MEM_SIZE);
    allocate_ram(sysmem, "edgeic", EDGEIC_MEM_BASE, EDGEIC_MEM_SIZE);

    nms->reset_pc = SDRAM_MEM_BASE;

    /*
     * IRAM0 also appears at address 0, which is where the ARM926 takes its
     * exceptions from. Rockbox's crt0.S is explicit about it -- "copy vector
     * section to start of IRAM0 (it will be mapped to 0x0)" and "we do not
     * want to execute from 0x0 as iram will be mapped there".
     *
     * What we do not know yet is whether the bootrom sits there first and the
     * alias only appears after a remap, the way the S5L8720 works. Until a
     * bootrom dump settles it, a supplied bootrom takes the address instead.
     */
    if (nms->bootrom_path[0] == '\0') {
        MemoryRegion *alias = g_new(MemoryRegion, 1);
        memory_region_init_alias(alias, NULL, "iram0-alias", iram0, 0,
                                 IRAM0_MEM_SIZE);
        memory_region_add_subregion(sysmem, 0, alias);
    }

    if (nms->bootrom_path[0] != '\0') {
        uint8_t *data = NULL;
        gsize size;

        if (!g_file_get_contents(nms->bootrom_path, (char **)&data, &size,
                                 NULL)) {
            error_report("could not read bootrom \"%s\"", nms->bootrom_path);
            exit(1);
        }
        if (size > BOOTROM_MEM_SIZE) {
            error_report("bootrom is 0x%zx bytes, the window at 0x%x is only "
                         "0x%x", (size_t)size, (unsigned)BOOTROM_MEM_BASE,
                         (unsigned)BOOTROM_MEM_SIZE);
            exit(1);
        }
        allocate_ram(sysmem, "bootrom", BOOTROM_MEM_BASE, BOOTROM_MEM_SIZE);
        address_space_rw(nsas, BOOTROM_MEM_BASE, MEMTXATTRS_UNSPECIFIED, data,
                         size, 1);
        g_free(data);
        nms->reset_pc = BOOTROM_MEM_BASE;
    }

    /*
     * -kernel takes either an ELF or a raw binary, and wins over the bootrom
     * so an image can be booted directly while a bootrom is still supplied for
     * whatever it may map.
     *
     * An ELF carries its own load addresses and entry point, which matters
     * because the two things we want to boot land in different places: a
     * Rockbox bootloader runs from IRAM1 at 0x22020800, while the osos image
     * of a retail firmware loads at 0x08000000 (addr in the firmware
     * directory, see wiki/01-firmware-ipsw.md). A raw binary has no such
     * information, so it goes to SDRAM.
     */
    if (machine->kernel_filename) {
        uint64_t entry = 0, lowaddr = 0, highaddr = 0;
        ssize_t size;

        size = load_elf(machine->kernel_filename, NULL, NULL, NULL, &entry,
                        &lowaddr, &highaddr, NULL, 0, EM_ARM, 1, 0);
        if (size > 0) {
            info_report("loaded ELF \"%s\": 0x%" PRIx64 "-0x%" PRIx64
                        ", entry 0x%" PRIx64, machine->kernel_filename,
                        lowaddr, highaddr, entry);
            nms->reset_pc = entry;
        } else {
            size = load_image_targphys(machine->kernel_filename,
                                       SDRAM_MEM_BASE, SDRAM_MEM_SIZE);
            if (size < 0) {
                error_report("could not load \"%s\" at 0x%x",
                             machine->kernel_filename,
                             (unsigned)SDRAM_MEM_BASE);
                exit(1);
            }
            info_report("loaded 0x%zx raw bytes at 0x%x", (size_t)size,
                        (unsigned)SDRAM_MEM_BASE);
            nms->reset_pc = SDRAM_MEM_BASE;
        }
    }
}

static char *ipod_classic_get_bootrom_path(Object *obj, Error **errp)
{
    IPodClassicMachineState *nms = IPOD_CLASSIC_MACHINE(obj);
    return g_strdup(nms->bootrom_path);
}

static void ipod_classic_set_bootrom_path(Object *obj, const char *value,
                                          Error **errp)
{
    IPodClassicMachineState *nms = IPOD_CLASSIC_MACHINE(obj);

    if (!g_file_test(value, G_FILE_TEST_EXISTS)) {
        error_report("bootrom at path \"%s\" must exist", value);
        exit(1);
    }
    g_strlcpy(nms->bootrom_path, value, sizeof(nms->bootrom_path));
}

static bool ipod_classic_get_semihosting(Object *obj, Error **errp)
{
    return IPOD_CLASSIC_MACHINE(obj)->semihosting;
}

static void ipod_classic_set_semihosting(Object *obj, bool value, Error **errp)
{
    IPOD_CLASSIC_MACHINE(obj)->semihosting = value;
}

static void ipod_classic_instance_init(Object *obj)
{
    object_property_add_str(obj, "bootrom", ipod_classic_get_bootrom_path,
                            ipod_classic_set_bootrom_path);
    object_property_set_description(obj, "bootrom",
                                    "Path to the S5L8702 bootrom binary");

    /*
     * Apple's retailOS is built against ARM's own C library with the
     * semihosting retarget left in: 131 "svc 0x123456" sites in the image,
     * 115 of them preceded by "mov r0, #4", which is SYS_WRITE0. It also ships
     * RTXCbug, the RTXC kernel's interactive debugger, whose prompt strings
     * ("** RTXCbug - ", "RTXCbug> ", "Bad command") sit at 0x0827bd79 and are
     * printed the same way. With semihosting off those SVCs go to a vector the
     * firmware may not have installed and the machine dies quietly; with it on
     * the firmware tells us what it thinks is wrong. Costs nothing when
     * nothing is printed, so it is on by default.
     */
    IPOD_CLASSIC_MACHINE(obj)->semihosting = true;
    object_property_add_bool(obj, "semihosting", ipod_classic_get_semihosting,
                             ipod_classic_set_semihosting);
    object_property_set_description(obj, "semihosting",
        "Answer ARM semihosting calls (SVC 0x123456). Apple's retailOS prints "
        "its diagnostics through them. Turn it off to see the firmware take "
        "the SVC vector instead.");
}

static inline qemu_irq s5l8702_get_irq(IPodClassicMachineState *s, int n)
{
    return s->irq[n / S5L8702_VIC_SIZE][n % S5L8702_VIC_SIZE];
}

/*
 * Peripherals we know the address of but have not modelled. Registering them
 * keeps stray accesses out of the "unassigned memory" path and makes -d unimp
 * report which block the guest is talking to.
 */
static const struct { const char *name; hwaddr base; hwaddr size; }
ipod_classic_unimplemented[] = {
    { "s5l8702.sha1",     SHA1_MEM_BASE,     0x100000 },
    { "s5l8702.miu",      MIU_MEM_BASE,      0x100000 },
    { "s5l8702.dmac0",    DMAC0_MEM_BASE,    0x100000 },
    { "s5l8702.lcd",      LCD_MEM_BASE,      0x100000 },
    { "s5l8702.usb-otg",  USBOTG_MEM_BASE,   0x100000 },
    { "s5l8702.sm1",      SM1_MEM_BASE,      0x100000 },
    { "s5l8702.usb-hc",   USB_HC_MEM_BASE,   0x100000 },
    { "s5l8702.ata",      ATA_MEM_BASE,      0x100000 },
    { "s5l8702.usb-fc",   USB_FC_MEM_BASE,   0x100000 },
    { "s5l8702.ata-unk",  ATA_UNK_MEM_BASE,  0x100000 },
    { "s5l8702.sdci",     SDCI_MEM_BASE,     0x100000 },
    { "s5l8702.aes",      AES_MEM_BASE,      0x100000 },
    { "s5l8702.adm-sfr",  ADM_SFR_MEM_BASE,  0x100000 },
    { "s5l8702.lcd-win",  LCD_WIN_MEM_BASE,  0x100000 },
    { "s5l8702.fmc",      FMC_MEM_BASE,      0x100000 },
    { "s5l8702.dmac1",    DMAC1_MEM_BASE,    0x100000 },
    { "s5l8702.eic",      EIC_MEM_BASE,      0x100000 },
    { "s5l8702.int",      INT_MEM_BASE,      0x100000 },
    { "s5l8702.ecc",      ECC_MEM_BASE,      0x100000 },
    { "s5l8702.wheel",    WHEEL_MEM_BASE,    0x100000 },
    { "s5l8702.usb-phy",  USBPHYS_MEM_BASE,  0x100000 },
    { "s5l8702.i2c0",     I2C0_MEM_BASE,     0x100000 },
    { "s5l8702.wdt",      WDT_MEM_BASE,      0x100000 },
    { "s5l8702.i2s",      I2S_MEM_BASE,      0x100000 },
    { "s5l8702.spdif",    SPDIF_MEM_BASE,    0x100000 },
    { "s5l8702.spi0",     SPI0_MEM_BASE,     0x100000 },
    { "s5l8702.adc",      ADC_MEM_BASE,      0x100000 },
    { "s5l8702.gpio",     GPIO_MEM_BASE,     0x100000 },
    { "s5l8702.chipid",   CHIPID_MEM_BASE,   0x100000 },
    { "s5l8702.rtc",      RTC_MEM_BASE,      0x100000 },
    { "s5l8702.adm",      ADM_MEM_BASE,      0x100000 },
};

/*
 * Everything in the peripheral window that is not one of the blocks above.
 * Same behaviour as create_unimplemented_device(), but one region for the
 * whole window and at a lower priority so the named blocks stay on top.
 */
static uint64_t ipod_classic_io_floor_read(void *opaque, hwaddr offset,
                                           unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "s5l8702.io: read  (size %d, addr 0x%08" PRIx64
                  ")\n", size, (uint64_t)(IO_WINDOW_BASE + offset));
    return 0;
}

static void ipod_classic_io_floor_write(void *opaque, hwaddr offset,
                                        uint64_t value, unsigned size)
{
    qemu_log_mask(LOG_UNIMP, "s5l8702.io: write (size %d, addr 0x%08" PRIx64
                  ", value 0x%" PRIx64 ")\n", size,
                  (uint64_t)(IO_WINDOW_BASE + offset), value);
}

static const MemoryRegionOps ipod_classic_io_floor_ops = {
    .read = ipod_classic_io_floor_read,
    .write = ipod_classic_io_floor_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 8,
};

static void ipod_classic_machine_init(MachineState *machine)
{
    IPodClassicMachineState *nms = IPOD_CLASSIC_MACHINE(machine);
    MemoryRegion *sysmem;
    AddressSpace *nsas;
    ARMCPU *cpu;
    DeviceState *dev;

    if (nms->semihosting) {
        qemu_semihosting_enable();
    }

    ipod_classic_cpu_setup(machine, &sysmem, &cpu, &nsas);
    nms->cpu = cpu;
    nms->nsas = nsas;

    nms->sysclk = clock_new(OBJECT(machine), "SYSCLK");
    clock_set_hz(nms->sysclk, 12000000ULL);

    ipod_classic_memory_setup(machine, sysmem, nsas);

    /* VIC0 drives the CPU, VIC1 daisy-chains into it. */
    nms->irq = g_malloc0(sizeof(qemu_irq *) * S5L8702_VIC_N);

    dev = pl192_manual_init("vic0",
                            qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_IRQ),
                            qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_FIQ), NULL);
    nms->vic0 = PL192(dev);
    memory_region_add_subregion(sysmem, VIC0_MEM_BASE, &nms->vic0->iomem);
    nms->irq[0] = g_malloc0(sizeof(qemu_irq) * S5L8702_VIC_SIZE);
    for (int i = 0; i < S5L8702_VIC_SIZE; i++) {
        nms->irq[0][i] = qdev_get_gpio_in(dev, i);
    }

    dev = pl192_manual_init("vic1", NULL);
    nms->vic1 = PL192(dev);
    memory_region_add_subregion(sysmem, VIC1_MEM_BASE, &nms->vic1->iomem);
    nms->irq[1] = g_malloc0(sizeof(qemu_irq) * S5L8702_VIC_SIZE);
    for (int i = 0; i < S5L8702_VIC_SIZE; i++) {
        nms->irq[1][i] = qdev_get_gpio_in(dev, i);
    }

    nms->vic1->daisy = nms->vic0;

    /* Clock and PLL block. */
    dev = qdev_new(TYPE_IPOD_CLASSIC_CLK);
    nms->clock0 = IPOD_CLASSIC_CLK(dev);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CLOCK0_MEM_BASE);

    /*
     * Timers. Channels A..D share IRQ 8 and E..H share IRQ 7; Rockbox drives
     * B for the 10 kHz kernel tick and F for the user timer.
     */
    dev = qdev_new(TYPE_IPOD_CLASSIC_TIMER);
    nms->timer = IPOD_CLASSIC_TIMER(dev);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, TIMER_MEM_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                       s5l8702_get_irq(nms, S5L8702_TIMER_IRQ));
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 1,
                       s5l8702_get_irq(nms, S5L8702_TIMER32_IRQ));

    /* Serial console. */
    exynos4210_uart_create(UART0_MEM_BASE, 256, 0, serial_hd(0),
                           s5l8702_get_irq(nms, S5L8702_UART0_IRQ));

    for (size_t i = 0; i < ARRAY_SIZE(ipod_classic_unimplemented); i++) {
        create_unimplemented_device(ipod_classic_unimplemented[i].name,
                                    ipod_classic_unimplemented[i].base,
                                    ipod_classic_unimplemented[i].size);
    }

    /*
     * A floor under the whole peripheral window. The retailOS maps
     * 0x38000000..0x3fffffff as one 768 MB uncached identity range in the page
     * table it builds at 0x22004534, so a register we have not listed above is
     * not a harmless zero read: it is a data abort, taken through a handler
     * that lives in DRAM and will cascade. Priority -2000 puts it below the
     * per-block windows, which are themselves at -1000.
     */
    MemoryRegion *io_floor = g_new(MemoryRegion, 1);
    memory_region_init_io(io_floor, NULL, &ipod_classic_io_floor_ops, NULL,
                          "s5l8702.io", IO_WINDOW_SIZE);
    memory_region_add_subregion_overlap(sysmem, IO_WINDOW_BASE, io_floor,
                                        -2000);

    qemu_register_reset(ipod_classic_cpu_reset, nms);
}

static void ipod_classic_machine_class_init(ObjectClass *klass, void *data)
{
    MachineClass *mc = MACHINE_CLASS(klass);

    mc->desc = "Apple iPod classic (6th generation), S5L8702";
    mc->init = ipod_classic_machine_init;
    mc->max_cpus = 1;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm926");
}

static const TypeInfo ipod_classic_machine_info = {
    .name          = TYPE_IPOD_CLASSIC_MACHINE,
    .parent        = TYPE_MACHINE,
    .instance_size = sizeof(IPodClassicMachineState),
    .class_size    = sizeof(IPodClassicMachineClass),
    .class_init    = ipod_classic_machine_class_init,
    .instance_init = ipod_classic_instance_init,
};

static void ipod_classic_machine_types(void)
{
    type_register_static(&ipod_classic_machine_info);
}

type_init(ipod_classic_machine_types)
