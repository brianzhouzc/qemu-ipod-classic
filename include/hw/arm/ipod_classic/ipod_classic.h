/*
 * Apple iPod classic (6th generation) — Samsung S5L8702 SoC.
 *
 * The memory map below is derived from Rockbox's register header
 * firmware/export/s5l87xx.h, resolved for CONFIG_CPU == S5L8702. See
 * wiki/02-s5l8702-hardware.md for the derivation and the per-block notes.
 */

#ifndef HW_ARM_IPOD_CLASSIC_H
#define HW_ARM_IPOD_CLASSIC_H

#include "exec/hwaddr.h"
#include "hw/boards.h"
#include "hw/intc/pl192.h"
#include "hw/arm/boot.h"
#include "ui/console.h"
#include "cpu.h"
#include "hw/arm/ipod_touch_clock.h"

#define TYPE_IPOD_CLASSIC "iPod-Classic"

#define TYPE_IPOD_CLASSIC_MACHINE   MACHINE_TYPE_NAME(TYPE_IPOD_CLASSIC)
#define IPOD_CLASSIC_MACHINE(obj) \
    OBJECT_CHECK(IPodClassicMachineState, (obj), TYPE_IPOD_CLASSIC_MACHINE)

#define S5L8702_VIC_N     2
#define S5L8702_VIC_SIZE  32

/* RAM. DRAM_ORIG / IRAM_ORIG / IRAM0_SIZE / IRAM1_SIZE in s5l87xx.h. */
#define SDRAM_MEM_BASE    0x08000000
#define SDRAM_MEM_SIZE    0x04000000   /* 64 MB */
#define IRAM0_MEM_BASE    0x22000000
#define IRAM0_MEM_SIZE    0x00020000   /* 128 KB */
#define IRAM1_MEM_BASE    0x22020000
#define IRAM1_MEM_SIZE    0x00020000   /* 128 KB, only 64 KB on the S5L8720 */

/*
 * The bootrom base is not documented by Rockbox (it never runs there). The
 * S5L8720 maps it at 0, and the S5L8702 reset vector is assumed to be the
 * same until we can check against a dump. See wiki/02, "pendiente de confirmar".
 */
#define BOOTROM_MEM_BASE  0x00000000
#define BOOTROM_MEM_SIZE  0x00020000

/* Peripherals, IO_BASE 0x38000000. */
#define SHA1_MEM_BASE     0x38000000   /* SHA1_BASE */
#define MIU_MEM_BASE      0x38100000   /* MIU_BASE, memory interface unit */
#define DMAC0_MEM_BASE    0x38200000   /* DMA0_BASE, PL080 */
#define LCD_MEM_BASE      0x38300000   /* LCD_BASE */
#define USBOTG_MEM_BASE   0x38400000   /* OTGBASE, Synopsys DWC */
#define SM1_MEM_BASE      0x38500000   /* SM1_BASE, clock divider */
#define USB_HC_MEM_BASE   0x38600000   /* USB_HC_BASE */
#define ATA_MEM_BASE      0x38700000   /* ATA_BASE, the hard disk */
#define USB_FC_MEM_BASE   0x38800000   /* USB_FC_BASE */
#define ATA_UNK_MEM_BASE  0x38A00000   /* ATA_UNKNOWN_BASE */
#define SDCI_MEM_BASE     0x38B00000   /* SDCI_BASE, CE-ATA */
#define AES_MEM_BASE      0x38C00000   /* AES_BASE */
#define VIC0_MEM_BASE     0x38E00000   /* VIC_BASE */
#define VIC1_MEM_BASE     0x38E01000
#define ADM_SFR_MEM_BASE  0x39000000   /* ADM_SFR_BASE, CalmADM2E audio DSP */
#define LCD_WIN_MEM_BASE  0x39200000   /* LCDBASE, framebuffer windows */
#define FMC_MEM_BASE      0x39400000   /* FMC_BASE, NAND controller */
#define DMAC1_MEM_BASE    0x39900000   /* DMA1_BASE, PL080 */
#define EIC_MEM_BASE      0x39A00000   /* EIC_BASE / SYSALV_BASE */
#define INT_MEM_BASE      0x39C00000   /* INT_BASE, legacy interrupt ctrl */
#define ECC_MEM_BASE      0x39E00000   /* ECC_BASE */
#define WHEEL_MEM_BASE    0x3C200000   /* WHEEL_BASE, click wheel */
#define USBPHYS_MEM_BASE  0x3C400000   /* USB_PHY_BASE */
#define CLOCK0_MEM_BASE   0x3C500000   /* CLK_BASE */
#define I2C0_MEM_BASE     0x3C600000   /* IIC_BASE */
#define TIMER_MEM_BASE    0x3C700000   /* TIMER_BASE */
#define WDT_MEM_BASE      0x3C800000   /* WDT_BASE */
#define I2S_MEM_BASE      0x3CA00000   /* I2S_BASE */
#define SPDIF_MEM_BASE    0x3CB00000   /* SPD_BASE */
#define UART0_MEM_BASE    0x3CC00000   /* UARTC_BASE_ADDR */
#define SPI0_MEM_BASE     0x3CD00000   /* SPI_BASE */
#define ADC_MEM_BASE      0x3CE00000   /* ADC_BASE */
#define GPIO_MEM_BASE     0x3CF00000   /* GPIO_BASE */
#define CHIPID_MEM_BASE   0x3D100000   /* CHIPID_BASE */
#define RTC_MEM_BASE      0x3D200000   /* RTC_BASE, also SPIBASE(2) */
#define ADM_MEM_BASE      0x3F000000   /* ADM_BASE */

/* IRQ numbers, from the IRQ_* macros in s5l87xx.h. */
#define S5L8702_TIMER32_IRQ  7
#define S5L8702_TIMER_IRQ    8
#define S5L8702_SPI0_IRQ     9
#define S5L8702_SPI1_IRQ     10
#define S5L8702_SPI2_IRQ     11
#define S5L8702_LCD_IRQ      14
#define S5L8702_DMAC0_IRQ    16
#define S5L8702_DMAC1_IRQ    17
#define S5L8702_USB_FUNC_IRQ 19
#define S5L8702_NAND_IRQ     20
#define S5L8702_I2C0_IRQ     21
#define S5L8702_I2C1_IRQ     22
#define S5L8702_WHEEL_IRQ    23
#define S5L8702_UART0_IRQ    24
#define S5L8702_UART1_IRQ    25
#define S5L8702_UART2_IRQ    26
#define S5L8702_UART3_IRQ    27
#define S5L8702_UART4_IRQ    28
#define S5L8702_ATA_IRQ      29
#define S5L8702_SECBOOT_IRQ  36
#define S5L8702_AES_IRQ      39
#define S5L8702_SHA_IRQ      40
#define S5L8702_NANDECC_IRQ  43
#define S5L8702_MMC_IRQ      44

typedef struct {
    MachineClass parent;
} IPodClassicMachineClass;

typedef struct {
    MachineState parent;
    AddressSpace *nsas;
    qemu_irq **irq;
    ARMCPU *cpu;
    PL192State *vic0;
    PL192State *vic1;
    IPodTouchClockState *clock0;
    Clock *sysclk;
    /*
     * Where the CPU starts executing after reset. Either BOOTROM_MEM_BASE when
     * a bootrom is supplied, or SDRAM_MEM_BASE when a raw image is loaded with
     * -kernel.
     */
    hwaddr reset_pc;
    char bootrom_path[1024];
} IPodClassicMachineState;

#endif
