/*
 * Broadcom BCM2722 video companion chip of the iPod 5G.
 *
 * See hw/arm/ipod_video/ipod_video_bcm.c for the protocol and
 * wiki/07-pp502x-hardware.md for where it sits in the memory map.
 */

#ifndef HW_ARM_IPOD_VIDEO_BCM_H
#define HW_ARM_IPOD_VIDEO_BCM_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "ui/console.h"
#include "qom/object.h"

#define TYPE_IPOD_VIDEO_BCM "ipod-video.bcm2722"
OBJECT_DECLARE_SIMPLE_TYPE(IPodVideoBCMState, IPOD_VIDEO_BCM)

/*
 * Eight registers 64 KB apart, from BCM_DATA at +0 through BCM_ALT_CONTROL at
 * +0x70000 in Rockbox's lcd-video.c.
 */
#define BCM_MMIO_SIZE   0x80000

/*
 * The chip's own address space. Rockbox writes frames at BCMA_CMDPARAM
 * (0xE0000) and the panel is 320x240 RGB565, so a full frame needs 150 KB from
 * there; round the window up well past that to leave room for the registers
 * and scratch the firmware pokes at during init.
 */
#define BCM_MEM_SIZE    0x00200000

#define LCD_WIDTH       320
#define LCD_HEIGHT      240

struct IPodVideoBCMState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;
    QemuConsole *con;

    uint8_t *mem;           /* the chip's address space */
    uint32_t wr_addr;       /* auto-incrementing write pointer */
    uint32_t rd_addr;       /* auto-incrementing read pointer */
    bool dirty;
};

#endif
