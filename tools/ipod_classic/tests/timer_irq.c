/*
 * Bare-metal test for the iPod-Classic machine: drive timer B at 10 kHz and
 * check that its interrupt reaches the CPU through VIC0.
 *
 * This exercises the whole chain the real firmware depends on -- timer model,
 * IRQ wiring, PL192, the CPU's IRQ input and the exception vectors -- which
 * the hello-world binary does not touch at all.
 *
 * Build with tools/ipod_classic/tests/build.sh, run with:
 *   qemu-system-arm -M iPod-Classic -kernel timer_irq.bin \
 *       -display none -monitor none -serial stdio
 */

#include <stdint.h>

#define REG32(a) (*(volatile uint32_t *)(a))

/* S5L8702 peripherals, see wiki/02-s5l8702-hardware.md. */
#define UART0_BASE  0x3CC00000
#define UART0_UTXH  (UART0_BASE + 0x20)

#define VIC0_BASE       0x38E00000
#define VIC_IRQSTATUS   0x00
#define VIC_INTENABLE   0x10
#define VIC_VECTADDR    0xF00

#define TIMER_BASE  0x3C700000
#define TBCON       REG32(TIMER_BASE + 0x20)
#define TBCMD       REG32(TIMER_BASE + 0x24)
#define TBDATA0     REG32(TIMER_BASE + 0x28)
#define TBPRE       REG32(TIMER_BASE + 0x30)
#define TBCNT       REG32(TIMER_BASE + 0x34)

#define IRQ_TIMER   8       /* shared by the 16-bit timers A..D */

#define TICKS_WANTED 5

static volatile uint32_t ticks;

static void putch(char c)
{
    REG32(UART0_UTXH) = (uint32_t)(unsigned char)c;
}

static void puts_(const char *s)
{
    while (*s) {
        putch(*s++);
    }
}

static void puthex(uint32_t v)
{
    static const char digits[] = "0123456789abcdef";
    puts_("0x");
    for (int i = 28; i >= 0; i -= 4) {
        putch(digits[(v >> i) & 0xf]);
    }
}

/* Called from the IRQ vector in start.S. */
void irq_handler_c(void)
{
    /*
     * Reading VECTADDR acknowledges the interrupt in the PL192; writing it
     * back signals end-of-interrupt.
     */
    uint32_t vect = REG32(VIC0_BASE + VIC_VECTADDR);

    /*
     * Acknowledge the timer. On the 16-bit timers the interrupt status bits
     * live in CON at 16..18 and are cleared by writing them back, which is
     * exactly what Rockbox's tick handler does.
     */
    TBCON = TBCON;

    ticks++;

    REG32(VIC0_BASE + VIC_VECTADDR) = vect;
}

int main(void)
{
    puts_("S5L8702 timer test\r\n");

    /* Unmask timer IRQ 8 in VIC0. */
    REG32(VIC0_BASE + VIC_INTENABLE) = 1u << IRQ_TIMER;

    /*
     * Timer B at 10 kHz, same configuration Rockbox uses for its kernel tick:
     * 12 MHz ECLK / 16 / 75. Fire every 10 counts, i.e. every millisecond.
     */
    TBCMD = (1 << 1);           /* TB_CLR: initialise the timer */
    TBPRE = 75 - 1;
    TBCON = (0 << 13) |         /* TB_INT1_EN  */
            (1 << 12) |         /* TB_INT0_EN  */
            (0 << 11) |         /* TB_START    */
            (2 << 8)  |         /* TB_CS = ECLK / 16 */
            (1 << 6)  |         /* select ECLK */
            (0 << 4);           /* interval mode */
    TBDATA0 = 10;
    TBCMD = (1 << 0);           /* TB_EN */

    puts_("counter runs: ");
    uint32_t a = TBCNT;
    for (volatile int i = 0; i < 2000; i++) {
        /* spin so the counter has time to move */
    }
    uint32_t b = TBCNT;
    puts_(b != a ? "yes " : "NO ");
    puthex(a);
    putch(' ');
    puthex(b);
    puts_("\r\n");

    puts_("waiting for interrupts...\r\n");
    while (ticks < TICKS_WANTED) {
        /* the IRQ handler counts them */
    }

    puts_("got ");
    puthex(ticks);
    puts_(" timer interrupts\r\n");
    puts_("TIMER IRQ OK\r\n");

    for (;;) {
        /* done */
    }
}
