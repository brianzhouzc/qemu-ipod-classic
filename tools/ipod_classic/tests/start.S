/*
 * Startup for the iPod-Classic bare-metal tests.
 *
 * The machine drops the image at 0x08000000 (SDRAM) and starts the CPU there,
 * so this is the entry point. The ARM926 takes its exceptions from 0x00000000
 * though, and on the S5L8702 that address aliases IRAM0 -- Rockbox's crt0.S
 * says as much: "copy vector section to start of IRAM0 (it will be mapped to
 * 0x0)". So the first thing we do is install a vector table there.
 */

    .section .text.start, "ax"
    .arm
    .global _start
_start:
    /* Supervisor mode, interrupts off while we set up. */
    msr     cpsr_c, #0xd3

    /* Copy the vector table to 0x0, which aliases IRAM0. */
    ldr     r0, =vectors
    mov     r1, #0
    mov     r2, #16 * 4                 /* 8 vectors + 8 literals */
1:  ldr     r3, [r0], #4
    str     r3, [r1], #4
    subs    r2, r2, #4
    bne     1b

    /* IRQ mode stack, then back to supervisor mode with its own stack. */
    msr     cpsr_c, #0xd2               /* IRQ mode, interrupts still off */
    ldr     sp, =irq_stack_top
    msr     cpsr_c, #0xd3               /* supervisor mode */
    ldr     sp, =svc_stack_top

    /* Zero .bss so `ticks` starts at 0. */
    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
2:  cmp     r0, r1
    strlo   r2, [r0], #4
    blo     2b

    /* Enable IRQs and go. */
    msr     cpsr_c, #0x13               /* supervisor mode, IRQ enabled */
    bl      main
3:  b       3b

/*
 * The table copied to address 0. Each entry is a PC-relative load, and the
 * literal pool right after it is copied along with the branches, so the loads
 * still resolve once the table sits at 0.
 */
    .align 5
vectors:
    ldr     pc, [pc, #24]               /* 0x00 reset          */
    ldr     pc, [pc, #24]               /* 0x04 undefined      */
    ldr     pc, [pc, #24]               /* 0x08 SWI            */
    ldr     pc, [pc, #24]               /* 0x0c prefetch abort */
    ldr     pc, [pc, #24]               /* 0x10 data abort     */
    ldr     pc, [pc, #24]               /* 0x14 reserved       */
    ldr     pc, [pc, #24]               /* 0x18 IRQ            */
    ldr     pc, [pc, #24]               /* 0x1c FIQ            */
    .word   _start                      /* 0x20 */
    .word   hang                        /* 0x24 */
    .word   hang                        /* 0x28 */
    .word   hang                        /* 0x2c */
    .word   hang                        /* 0x30 */
    .word   hang                        /* 0x34 */
    .word   irq_entry                   /* 0x38 */
    .word   hang                        /* 0x3c */

hang:
    b       hang

irq_entry:
    sub     lr, lr, #4
    stmfd   sp!, {r0-r3, r12, lr}
    bl      irq_handler_c
    ldmfd   sp!, {r0-r3, r12, pc}^

    .section .bss
    .align 3
    .space  4096
irq_stack_top:
    .space  8192
svc_stack_top:
