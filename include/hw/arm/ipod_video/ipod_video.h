/*
 * Apple iPod 5th generation ("Video") — PortalPlayer PP5020/PP5022 SoC.
 *
 * Addresses come from Rockbox's firmware/export/pp5020.h and the boot model
 * from bootloader/ipod.c. See wiki/07-pp502x-hardware.md for the derivation
 * and wiki/06-ipod-video-5g.md for the device this was taken off.
 */

#ifndef HW_ARM_IPOD_VIDEO_H
#define HW_ARM_IPOD_VIDEO_H

#include "exec/hwaddr.h"
#include "hw/boards.h"
#include "hw/arm/boot.h"
#include "cpu.h"
#include "qemu/timer.h"
#include "hw/qdev-core.h"
#include "exec/memory.h"

#define TYPE_IPOD_VIDEO "iPod-Video"

#define TYPE_IPOD_VIDEO_MACHINE   MACHINE_TYPE_NAME(TYPE_IPOD_VIDEO)
#define IPOD_VIDEO_MACHINE(obj) \
    OBJECT_CHECK(IPodVideoMachineState, (obj), TYPE_IPOD_VIDEO_MACHINE)

/*
 * DRAM_START in pp5020.h, and the same value the firmware partition's
 * directory carries in its addr field for every image.
 */
#define DRAM_MEM_BASE       0x10000000
#define DRAM_MEM_SIZE       0x04000000   /* 64 MB, what this unit reports */

/*
 * IRAMORIG in Rockbox's app-pp.lds. That script only claims 0xc000 for the
 * application, which is a budget rather than the size of the hardware; the
 * real figure is not confirmed, so this is deliberately generous.
 */
#define IRAM_MEM_BASE       0x40000000
#define IRAM_MEM_SIZE       0x00020000

/*
 * The PP's NOR flash. It is what address 0 answers to while the bootloader
 * runs; afterwards it is remapped to 0x20000000. Rockbox's hwcompat.h reads
 * the board revision from 0x405c through the first window and 0x2000405c
 * through the second.
 */
#define FLASH_ROM_MEM_BASE      0x00000000
#define FLASH_ROM_MEM_SIZE      0x00100000
#define FLASH_ROM_REMAP_BASE    0x20000000
#define IPOD_HW_REVISION_OFF    0x405c

/*
 * What the device on the desk reports, straight out of Rockbox's HW info
 * screen. The top half identifies the family: 0x000b is what ipodpatcher
 * calls "Video (aka 5th Generation)".
 */
#define IPOD_VIDEO_HW_REVISION  0x000b0005

/* The Broadcom BCM2722 that owns the framebuffer and drives the panel. */
#define BCM_MEM_BASE        0x30000000

/* Peripherals. */
#define PROC_ID_MEM_BASE    0x60000000   /* PROCESSOR_ID: 0x55 = CPU, 0xaa = COP */
#define INTC_MEM_BASE       0x60004000   /* CPU_INT_EN at +0x24, hi bank +0x120 */
#define TIMER_MEM_BASE      0x60005000   /* TIMER1_CFG +0x00, USEC_TIMER +0x10 */
#define CLOCK_MEM_BASE      0x60006000   /* DEV_EN +0x0c, PLL_CONTROL +0x34 */
#define PROC_CTL_MEM_BASE   0x60007000   /* CPU_CTL +0x00, COP_CTL +0x04 */
#define CACHE_CTL_MEM_BASE  0x6000c000   /* includes CACHE_CTL_VECT_REMAP */
#define GPIO_MEM_BASE       0x6000d000
#define DMA_MEM_BASE        0x6000a000
#define IDE_MEM_BASE        0xc3000000   /* the disk, IDE_IRQ 23 */
/*
 * ATA_DATA..ATA_COMMAND live at IDE_BASE + 0x1e0 four bytes apart and
 * ATA_CONTROL at +0x3f8, per Rockbox's ata-target.h. Everything below the
 * task file is PP-specific timing and configuration.
 */
#define IDE_TASKFILE_OFF    0x1e0
#define IDE_CONTROL_OFF     0x3f8
#define DEV_CTL_MEM_BASE    0x70000000   /* DEV_INIT1/2, GPO32_VAL, GPO32_ENABLE */
/*
 * The click wheel, not I2C as this was first labelled. Rockbox reads
 * CLICKWHEEL_DATA from 0x7000c140 in button-clickwheel.c, and the trace shows
 * the firmware reading exactly that offset. This is both why the firmware may
 * believe buttons are held and the device needed to navigate anything.
 */
#define CLICKWHEEL_MEM_BASE 0x7000c000
#define CLICKWHEEL_DATA_OFF 0x140
#define I2S_MEM_BASE        0x70002800
#define LCD_MEM_BASE        0x70003000
#define SER0_MEM_BASE       0x70006000   /* 16550-compatible, stride 4 */
#define SER1_MEM_BASE       0x70006040
#define USB_MEM_BASE        0xc5000000
/*
 * The cache's own arrays, which are addressable memory rather than registers:
 * data at 0xf0000000, its mirror at 0xf0002000, and the per-core line status
 * at 0xf0004000 (CPU) and 0xf0006000 (COP), with flush and invalidate windows
 * at 0xf0008000 and 0xf000c000. Rockbox writes the whole status array by hand
 * in cache_invalidate_special(), so it has to be backed by something.
 */
#define CACHE_ARRAY_MEM_BASE    0xf0000000
#define CACHE_ARRAY_MEM_SIZE    0x0000f000   /* up to the remap unit */
#define MMAP_MEM_BASE       0xf000f000   /* remap unit, MMAP0..7 */

/* IRQ numbers from pp5020.h. The high bank starts at 32. */
#define PP_IDE_IRQ          23
#define PP_SER0_IRQ         (32 + 4)
/* The click wheel shares the I2C line; see button-clickwheel.c. */
#define PP_I2C_IRQ          (32 + 8)

/* Identity read back by each core so it can tell which one it is. */
#define PP_PROC_ID_CPU      0x55
#define PP_PROC_ID_COP      0xaa

/*
 * The two cores, in the order QEMU creates them, so cpu_index doubles as the
 * core number. Both start at the same reset vector and the firmware sorts
 * itself out by reading PROCESSOR_ID.
 */
#define PP_CORE_CPU         0
#define PP_CORE_COP         1
#define PP_NUM_CORES        2

/*
 * CPU_CTL / COP_CTL bits. Bit 31 doubles as the "this core is asleep" status
 * and as the request to put it to sleep; crt0-pp-bl.S spins on the COP's until
 * it is set before touching anything else.
 */
#define PP_PROC_SLEEPING    0x80000000
#define PP_PROC_CTL_CPU     0x00
#define PP_PROC_CTL_COP     0x04

typedef struct {
    MachineClass parent;
} IPodVideoMachineClass;

typedef struct {
    MachineState parent;
    AddressSpace *nsas;
    ARMCPU *cpu[PP_NUM_CORES];
    /*
     * The ROM loader copies the whole osos image to DRAM_START and jumps to
     * DRAM_START + entryOffset, which is where ipodpatcher put the Rockbox
     * bootloader. Offset 0 runs Apple's firmware instead.
     */
    uint32_t entry_offset;
    hwaddr reset_pc;
    /* CPU_CTL and COP_CTL, indexed by core. The sleep bit is not stored
     * here: it is read back from the core's own halted state. */
    uint32_t proc_ctl[PP_NUM_CORES];
    uint32_t timer1_cfg;
    uint32_t timer2_cfg;
    uint32_t hw_revision;
    DeviceState *intc;
    QEMUTimer *timer1;
    QEMUTimer *timer2;
    bool timer_pending[2];
    uint32_t cache_ctl;
    MemoryRegion *vect_remap;
    MemoryRegion *dram_remap;
    bool dram_remapped;
    bool force_remap;
    uint32_t mmap_regs[1024];
    uint32_t gpio_regs[1024];
    uint32_t wheel_regs[1024];
    uint32_t wheel_buttons;
    bool wheel_pending;
    bool wheel_touched;
    uint32_t wheel_position;
    uint32_t clock_regs[1024];
    uint32_t dev_regs[1024];
} IPodVideoMachineState;

#endif
