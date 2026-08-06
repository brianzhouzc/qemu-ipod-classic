/*
 * S5L8702 timer block, 0x3C700000 (TIMER_BASE).
 *
 * Eight channels. A..D are 16-bit and share IRQ 8; E..H are 32-bit and share
 * IRQ 7. Register layout and semantics from Rockbox: the block comment above
 * TSTAT in firmware/export/s5l87xx.h, plus the two drivers that actually
 * drive it, timer-s5l8702.c (timer F) and kernel-s5l8702.c (timer B).
 */

#ifndef HW_ARM_IPOD_CLASSIC_TIMER_H
#define HW_ARM_IPOD_CLASSIC_TIMER_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/ptimer.h"
#include "qom/object.h"

#define TYPE_IPOD_CLASSIC_TIMER "s5l8702.timer"
OBJECT_DECLARE_SIMPLE_TYPE(IPodClassicTimerState, IPOD_CLASSIC_TIMER)

#define S5L8702_TIMER_MEM_SIZE  0x11C

#define S5L8702_NUM_TIMERS      8    /* A..H */
#define S5L8702_NUM_TIMERS_16   4    /* A..D, the 16-bit ones */

/* Per-channel registers, relative to the channel base. */
#define S5L8702_TIMER_CON       0x00
#define S5L8702_TIMER_CMD       0x04
#define S5L8702_TIMER_DATA0     0x08
#define S5L8702_TIMER_DATA1     0x0C
#define S5L8702_TIMER_PRE       0x10
#define S5L8702_TIMER_CNT       0x14
#define S5L8702_TIMER_STRIDE    0x20

/* Channel bases: A..D start at 0, E..H at 0xA0. */
#define S5L8702_TIMER_AD_BASE   0x00
#define S5L8702_TIMER_EH_BASE   0xA0

/* Shared status register for the 32-bit timers. */
#define S5L8702_TIMER_TSTAT     0x118

/*
 * CON bits. The three interrupt enables sit at 12..14 (INT0, INT1, INTOVF).
 * For A..D the matching status bits are at 16..18 in the same register, which
 * is why Rockbox tests "TACON & (TACON >> 4) & 0x7000". For E..H the status
 * lives in TSTAT instead.
 */
#define S5L8702_TIMER_CON_INT_EN_SHIFT    12
#define S5L8702_TIMER_CON_INT_STAT_SHIFT  16
#define S5L8702_TIMER_CON_INT_MASK        0x7
#define S5L8702_TIMER_CON_CS_SHIFT        8     /* clock select, 3 bits */
#define S5L8702_TIMER_CON_CS_MASK         0x7
#define S5L8702_TIMER_CON_ECLK            (1 << 6)
#define S5L8702_TIMER_CON_MODE_SHIFT      4

/* CMD bits. */
#define S5L8702_TIMER_CMD_EN     (1 << 0)
#define S5L8702_TIMER_CMD_CLR    (1 << 1)

/* TSTAT: three bits per 32-bit channel, E at 24, F at 16, G at 8, H at 0. */
#define S5L8702_TSTAT_SHIFT(ch32)  (24 - 8 * (ch32))

/*
 * ECLK, the external clock the iPod classic drives the timers from.
 * TIMER_FREQ in Rockbox's s5l87xx.h.
 */
#define S5L8702_TIMER_ECLK_HZ    12000000

typedef struct IPodClassicTimerState IPodClassicTimerState;

typedef struct {
    IPodClassicTimerState *parent;
    unsigned index;             /* 0..7, i.e. A..H */
    ptimer_state *ptimer;
    uint32_t con;
    uint32_t cmd;
    uint32_t data0;
    uint32_t data1;
    uint32_t pre;
} IPodClassicTimerChannel;

struct IPodClassicTimerState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    IPodClassicTimerChannel chan[S5L8702_NUM_TIMERS];
    uint32_t tstat;

    qemu_irq irq16;             /* IRQ 8, shared by A..D */
    qemu_irq irq32;             /* IRQ 7, shared by E..H */
};

#endif
