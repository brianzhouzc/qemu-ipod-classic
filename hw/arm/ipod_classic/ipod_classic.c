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

static void ipod_classic_cpu_reset(void *opaque)
{
    IPodClassicMachineState *nms = IPOD_CLASSIC_MACHINE((MachineState *)opaque);
    CPUState *cs = CPU(nms->cpu);

    cpu_reset(cs);
    cpu_set_pc(cs, nms->reset_pc);
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

static void ipod_classic_instance_init(Object *obj)
{
    object_property_add_str(obj, "bootrom", ipod_classic_get_bootrom_path,
                            ipod_classic_set_bootrom_path);
    object_property_set_description(obj, "bootrom",
                                    "Path to the S5L8702 bootrom binary");
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

static void ipod_classic_machine_init(MachineState *machine)
{
    IPodClassicMachineState *nms = IPOD_CLASSIC_MACHINE(machine);
    MemoryRegion *sysmem;
    AddressSpace *nsas;
    ARMCPU *cpu;
    DeviceState *dev;

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

    /* Clock/PLL block. Pure register file, reused from the iPod Touch model. */
    dev = qdev_new("ipodtouch.clock");
    nms->clock0 = IPOD_TOUCH_CLOCK(dev);
    memory_region_add_subregion(sysmem, CLOCK0_MEM_BASE, &nms->clock0->iomem);

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
