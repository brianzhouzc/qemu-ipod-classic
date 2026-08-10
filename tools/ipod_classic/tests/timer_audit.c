/*
 * Bare-metal AUDIT of the S5L8702 timer block as modelled by the
 * iPod-Classic machine.  Written for the timer audit of 2026-08-10; it does
 * not change anything, it only measures.
 *
 * It answers, one measurement per question:
 *
 *   1. Does a free-running counter advance at all, and at the right rate?
 *      Timer E is set up exactly as Rockbox's usec_timer_init() does
 *      (clocking-s5l8702.c:401), i.e. 1 MHz free-running, which is what
 *      s5l87xx.h:865 calls USEC_TIMER.  Timers F and G are set up as
 *      free-running counters at 12 MHz and 10 kHz and compared against it.
 *
 *   2. Does writing the interrupt-status bits back to CON -- the ack that
 *      Rockbox's INT_TIMERB does with "TBCON = TBCON", kernel-s5l8702.c:31 --
 *      reset the counter?  On the documented hardware only TB_CLR in CMD
 *      initialises the counter.
 *
 *   3. Same question for a write of DATA0 and of PRE with the value they
 *      already hold.
 *
 *   4. Is timer I (TICNTH/TICNTL at TIMER_BASE+0x80/0x84, s5l87xx.h:782)
 *      decoded at all?
 *
 *   5. How many timer-B interrupts actually arrive per second of USEC_TIMER
 *      time, versus the 1000 the programmed period asks for?
 *
 * Build with tools/ipod_classic/tests/build.sh, run with:
 *   qemu-system-arm -M iPod-Classic -kernel timer_audit.bin \
 *       -display none -monitor none -serial stdio
 */

#include <stdint.h>

#define REG32(a) (*(volatile uint32_t *)(a))

#define UART0_BASE  0x3CC00000
#define UART0_UTXH  (UART0_BASE + 0x20)

#define VIC0_BASE       0x38E00000
#define VIC_INTENABLE   0x10
#define VIC_VECTADDR    0xF00

#define TIMER_BASE  0x3C700000

#define TACON       REG32(TIMER_BASE + 0x00)
#define TBCON       REG32(TIMER_BASE + 0x20)
#define TBCMD       REG32(TIMER_BASE + 0x24)
#define TBDATA0     REG32(TIMER_BASE + 0x28)
#define TBPRE       REG32(TIMER_BASE + 0x30)
#define TBCNT       REG32(TIMER_BASE + 0x34)

#define TICNTH      REG32(TIMER_BASE + 0x80)
#define TICNTL      REG32(TIMER_BASE + 0x84)

#define TECON       REG32(TIMER_BASE + 0xA0)
#define TECMD       REG32(TIMER_BASE + 0xA4)
#define TEDATA0     REG32(TIMER_BASE + 0xA8)
#define TEPRE       REG32(TIMER_BASE + 0xB0)
#define TECNT       REG32(TIMER_BASE + 0xB4)

#define TFCON       REG32(TIMER_BASE + 0xC0)
#define TFCMD       REG32(TIMER_BASE + 0xC4)
#define TFDATA0     REG32(TIMER_BASE + 0xC8)
#define TFPRE       REG32(TIMER_BASE + 0xD0)
#define TFCNT       REG32(TIMER_BASE + 0xD4)

#define TGCON       REG32(TIMER_BASE + 0xE0)
#define TGCMD       REG32(TIMER_BASE + 0xE4)
#define TGDATA0     REG32(TIMER_BASE + 0xE8)
#define TGPRE       REG32(TIMER_BASE + 0xF0)
#define TGCNT       REG32(TIMER_BASE + 0xF4)

#define TSTAT       REG32(TIMER_BASE + 0x118)

#define IRQ_TIMER   8       /* shared by the 16-bit timers A..D */

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

static void putdec(uint32_t v)
{
    char buf[12];
    int n = 0;
    if (v == 0) {
        putch('0');
        return;
    }
    while (v) {
        buf[n++] = '0' + v % 10;
        v /= 10;
    }
    while (n--) {
        putch(buf[n]);
    }
}

void irq_handler_c(void)
{
    uint32_t vect = REG32(VIC0_BASE + VIC_VECTADDR);
    if (TSTAT & (0x07u << 16)) {
        TSTAT = 0x07u << 16;            /* timer F ack, does not touch CON */
    } else {
        TBCON = TBCON;                  /* the Rockbox timer-B ack */
    }
    ticks++;
    REG32(VIC0_BASE + VIC_VECTADDR) = vect;
}

/* Rockbox usec_timer_init(), clocking-s5l8702.c:400. */
static void usec_timer_init(void)
{
    TECON = (4 << 8) | (1 << 6) | (0 << 4);
    TEPRE = 12 - 1;                     /* 12 MHz ECLK -> 1 MHz */
    TEDATA0 = ~0u;
    TECMD = (1 << 1) | (1 << 0);
}

static void spin_us(uint32_t us)
{
    uint32_t start = TECNT;
    while ((uint32_t)(TECNT - start) < us) {
        /* USEC_TIMER busy wait, exactly what udelay() does */
    }
}

int main(void)
{
    puts_("S5L8702 timer audit\r\n");

    usec_timer_init();

    /* Timer F: free-running at ECLK/1 = 12 MHz. */
    TFCON = (4 << 8) | (1 << 6) | (0 << 4);
    TFPRE = 0;
    TFDATA0 = ~0u;
    TFCMD = (1 << 1) | (1 << 0);

    /* Timer G: free-running at ECLK/16/75 = 10 kHz. */
    TGCON = (2 << 8) | (1 << 6) | (0 << 4);
    TGPRE = 75 - 1;
    TGDATA0 = ~0u;
    TGCMD = (1 << 1) | (1 << 0);

    /* ---- 1. do the free-running counters advance, and at what rate? ---- */
    uint32_t e0 = TECNT, f0 = TFCNT, g0 = TGCNT;
    spin_us(200000);                    /* 200 ms of USEC_TIMER */
    uint32_t e1 = TECNT, f1 = TFCNT, g1 = TGCNT;

    puts_("free-run TECNT(1MHz)  d="); putdec(e1 - e0);
    puts_("  expect 200000\r\n");
    puts_("free-run TFCNT(12MHz) d="); putdec(f1 - f0);
    puts_("  expect 2400000\r\n");
    puts_("free-run TGCNT(10kHz) d="); putdec(g1 - g0);
    puts_("  expect 2000\r\n");

    /* ---- 4. is timer I decoded? ---- */
    uint32_t ih0 = TICNTH, il0 = TICNTL;
    spin_us(50000);
    uint32_t ih1 = TICNTH, il1 = TICNTL;
    puts_("TICNTH/L "); puthex(ih0); putch('/'); puthex(il0);
    puts_(" -> "); puthex(ih1); putch('/'); puthex(il1);
    puts_((ih0 != ih1 || il0 != il1) ? "  MOVES\r\n" : "  FROZEN\r\n");

    /* ---- timer B, 1 kHz: 12 MHz / 2 / 1, DATA0 = 6000, as retailOS ---- */
    TBCMD = 0;
    TBDATA0 = 6000;
    TBPRE = 0;
    TBCON = (1 << 12) | (0 << 8) | (1 << 6) | (0 << 4);
    TBCMD = (1 << 1);
    TBCMD = (1 << 0);

    /* ---- 2. does acking the status bits reset the counter? ---- */
    spin_us(500);
    uint32_t before = TBCNT;
    TBCON = TBCON;                      /* the Rockbox / retailOS ack */
    uint32_t after = TBCNT;
    puts_("ack TBCON=TBCON: TBCNT "); putdec(before);
    puts_(" -> "); putdec(after);
    puts_(after < before ? "   COUNTER WAS RESET\r\n" : "   kept counting\r\n");

    /* ---- 3. same for a rewrite of DATA0 and of PRE ---- */
    spin_us(500);
    before = TBCNT;
    TBDATA0 = 6000;
    after = TBCNT;
    puts_("rewrite TBDATA0: TBCNT "); putdec(before);
    puts_(" -> "); putdec(after);
    puts_(after < before ? "   COUNTER WAS RESET\r\n" : "   kept counting\r\n");

    spin_us(500);
    before = TBCNT;
    TBPRE = 0;
    after = TBCNT;
    puts_("rewrite TBPRE:   TBCNT "); putdec(before);
    puts_(" -> "); putdec(after);
    puts_(after < before ? "   COUNTER WAS RESET\r\n" : "   kept counting\r\n");

    /* Does the counter wrap at DATA0 or keep going? */
    spin_us(3000);
    puts_("TBCNT after 3 ms (DATA0=6000, 1 kHz): "); putdec(TBCNT);
    puts_("   (hardware wraps 0..5999)\r\n");

    puts_("TSTAT="); puthex(TSTAT);
    puts_("  TACON="); puthex(TACON);
    puts_("\r\n");

    /* ---- 5. interrupt rate against USEC_TIMER ---- */
    REG32(VIC0_BASE + VIC_INTENABLE) = 1u << IRQ_TIMER;
    TBCMD = (1 << 1) | (1 << 0);        /* restart clean */
    ticks = 0;
    uint32_t t0 = TECNT;
    spin_us(2000000);                   /* two seconds of USEC_TIMER */
    uint32_t t1 = TECNT;
    uint32_t got = ticks;
    puts_("timer B interrupts in "); putdec(t1 - t0);
    puts_(" us: "); putdec(got);
    puts_("   expect 2000\r\n");

    /*
     * ---- 6. the control experiment ----
     *
     * Timer F is a 32-bit timer: its interrupt status lives in TSTAT, not in
     * CON, so its handler acks with "TSTAT = 0x07 << 16" (Rockbox
     * timer-s5l8702.c:30) and never writes FCON.  Same 1 kHz period, same
     * VIC, same handler cost -- the only difference is that the ack does not
     * touch CON.  If this one keeps time and timer B does not, the CON write
     * is the whole story.
     */
    TBCMD = 0;                          /* stop timer B */
    REG32(VIC0_BASE + VIC_INTENABLE) = 1u << 7;   /* IRQ 7, timers E..H */
    TFCMD = 0;
    TFCON = (1 << 12) | (0 << 8) | (1 << 6) | (0 << 4);  /* INT0, ECLK/2 */
    TFPRE = 0;
    TFDATA0 = 6000;                     /* 6 MHz / 6000 = 1 kHz */
    TFCMD = (1 << 1) | (1 << 0);
    ticks = 0;
    t0 = TECNT;
    spin_us(2000000);
    t1 = TECNT;
    got = ticks;
    puts_("timer F interrupts in "); putdec(t1 - t0);
    puts_(" us: "); putdec(got);
    puts_("   expect 2000 (ack via TSTAT, CON untouched)\r\n");

    puts_("AUDIT DONE\r\n");

    for (;;) {
    }
}
