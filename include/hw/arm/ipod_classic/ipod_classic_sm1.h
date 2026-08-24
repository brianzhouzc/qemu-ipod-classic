/*
 * S5L8702 "SM1" audio clock/stream engine, 0x38500000 (SM1_BASE).
 *
 * Rockbox (firmware/export/s5l87xx.h) labels 0x38500000 "SM1" and guesses a
 * lone clock divider at +0x1000 (Clk_SM1 = HClk/(SM1_DIV+1), marked "TBC").
 * Rockbox never uses it -- it decodes on the ARM and clocks audio out of the
 * separate CLK_BASE/I2S/CS42L55 path. Apple's retailOS tells a different story:
 * SM1 is a rich audio engine with a run/power handshake that the audio bring-up
 * drives before it will feed the codec, and the retailOS NEVER touches +0x1000.
 *
 * The register map and state machine here were reverse-engineered from
 * osos_decrypted.bin (base VA = file offset + 0x07FF51B8) and cross-checked in
 * runtime with the pctrace mem-watch; see wiki/19-classic6g-dsp-hle.md section
 * 11. Every offset below is VERIFIED present in the disassembly (an accessor
 * reads/writes it); the ROLE of each is marked VERIFIED / INFERIDO / UNKNOWN in
 * the .c handlers. This device models the handshake coherently (state derived
 * from the writes) so ANY bring-up path -- the codec bring-up 0x0808170c or the
 * start/resume 0x08017378 -- completes without spinning, rather than the old
 * hand-faked "return magic + return bit2" window.
 */

#ifndef HW_ARM_IPOD_CLASSIC_SM1_H
#define HW_ARM_IPOD_CLASSIC_SM1_H

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_IPOD_CLASSIC_SM1 "s5l8702.sm1"
OBJECT_DECLARE_SIMPLE_TYPE(IPodClassicSM1State, IPOD_CLASSIC_SM1)

/*
 * The whole peripheral block is a 1 MiB window (SM1_MEM_BASE .. +0x100000), but
 * every register the retailOS touches lives in the low 4 KiB. We back that low
 * page with a register file for write latching and answer the rest as zero,
 * exactly like the create_unimplemented_device it replaces.
 */
#define S5L8702_SM1_MEM_SIZE   0x100000
#define S5L8702_SM1_REG_BYTES  0x1000
#define S5L8702_SM1_REG_WORDS  (S5L8702_SM1_REG_BYTES / 4)

/* ---- Top-level registers (offsets VERIFIED from the accessor disassembly) ---- */

/* Block identity. Getter 0x08075d64 reads this and compares against the magic;
 * a mismatch is a `b .` hang at 0x080819b8. VERIFIED role. */
#define S5L8702_SM1_ID          0xc80
#define S5L8702_SM1_ID_MAGIC    0x004e7309

/* Clock/engine status word. Getter 0x080a7638(idx0). The bring-up spins on
 * bit 2 ("clock running / locked") at 0x08081af8 (`tst r0,#4; beq`). VERIFIED
 * that bit 2 is the awaited bit; that it means "clock ready" is INFERIDO. */
#define S5L8702_SM1_STAT        0xa98
#define S5L8702_SM1_STAT_READY  (1u << 2)

/* Second status/counter word, getter 0x080a7638(idx1). Role UNKNOWN -> 0. */
#define S5L8702_SM1_STAT2       0xb18

/* Run/start control + run-state readback, both at +0x400.
 * Write: 0x080ae97c writes 1 to start. Read: 0x080b6788 polls (val & 3) until
 * it reads 1 ("running"). This is the start/resume path 0x08017378. VERIFIED
 * the two accessors; "start"/"running" is INFERIDO (strong). */
#define S5L8702_SM1_RUN         0x400
#define S5L8702_SM1_RUN_MASK    0x3

/* Status/flag, read by 0x080915a4 masked with bit 8. Role INFERIDO -> 0. */
#define S5L8702_SM1_FLAG404     0x404

/* Codec-enable strobe written during the bring-up (0x080a7614 writes 1).
 * INFERIDO: marks the engine powered. */
#define S5L8702_SM1_CODEC_EN    0x824

/* Control/command written 4 at the end of the codec bring-up (0x080a7680,
 * masked 0x7fff). INFERIDO: "configuration committed". */
#define S5L8702_SM1_COMMIT      0xc48

/*
 * Per-channel sub-blocks. Base offset for channel idx is computed by
 * 0x080bce04: idx 0..3 -> idx*0x20, idx4 -> 0x100, idx5 -> 0x180,
 * idx6 -> 0x200, idx7 -> 0x230. Within a sub-block:
 *   +0x10 (W, 0x080ba768 `str [r4,#16]`): state command. Values seen: 4
 *          (power-up), 8, 16 (run), 96.
 *   +0x14 (R, 0x080b8470 `ldr [r0,#20]; and #7`): state, polled until (&7)==7.
 * VERIFIED offsets/accessors; the command/state meanings are INFERIDO (strong).
 * The bring-up 0x0808170c uses idx4 (base+0x100): writes 4 to +0x110, waits for
 * +0x114 to read 7, then writes 16.
 */
#define S5L8702_SM1_SUB_CMD     0x10   /* within a sub-block */
#define S5L8702_SM1_SUB_STATE   0x14   /* within a sub-block */
#define S5L8702_SM1_SUB_STATE_READY 0x7

/*
 * Playback position registers. The audio driver reads the engine's per-channel
 * stream position from base + idx*0x14 + {0xa44,0xa48} (getter 0x08091710, idx
 * 0..3). +0xa48 is the position the PCM copy 0x08016e44 uses to decide WHERE in
 * the ring to write the next decoded block, and 0x080b3104 uses it to decide
 * whether a buffer region has been consumed. Left at zero (the old model), the
 * copy always targets offset 0 and playback never advances -- so no music ever
 * reaches the mixer. VERIFIED accessors; "playback position" is INFERIDO
 * (strong). We advance it from the I2S transmit-sample count (the real amount of
 * audio clocked out). See ipod_classic_sm1.c.
 */
#define S5L8702_SM1_POS_A48     0xa48   /* idx0; idx n at +0xa48 + n*0x14 */
#define S5L8702_SM1_POS_A44     0xa44   /* idx0 companion               */
#define S5L8702_SM1_POS_STRIDE  0x14
#define S5L8702_SM1_POS_CHANS   4

struct IPodClassicI2SState;

struct IPodClassicSM1State {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    uint32_t reg[S5L8702_SM1_REG_WORDS];

    /*
     * Derived engine state (not a hardware register): set once the audio
     * bring-up has powered the block -- a sub-block run command, the codec
     * enable at +0x824, or the +0x400 start. The status bit at +0xa98 bit2 and
     * the run-state at +0x400 are answered from it, so the poll loops end when
     * the firmware's own writes say the engine is up. See the .c for why this
     * is coherent rather than a hand-set constant.
     */
    bool powered;

    /*
     * The I2S the playback position tracks. The engine's stream position
     * advances as audio is clocked out, and in this model the I2S transmitter is
     * what actually clocks PCM out (paced by AUDIOPACE), so +0xa48 is derived
     * from its tx_samples. Set by the machine after both devices exist; may be
     * NULL (then the position stays 0, i.e. the old behaviour).
     */
    struct IPodClassicI2SState *i2s;

    /*
     * The engine's interrupt line, VIC0 line 18.
     *
     * Apple's retailOS installs a handler at 0x08017378 into the VIC handler
     * table at index 18 (registration path 0x0813e688 -> 0x080159c0 ->
     * IRAM 0x22004cc4, which stores the handler at table+8+id*4 and pokes
     * VIC0+0x14). That handler reads an SM1 register through the same getter
     * (0x080a7638) the audio bring-up uses, tests bit 2, and on success signals
     * the event at IRAM 0x22008c58 -- which is exactly the event the native
     * render pump (IRAM 0x22006118) waits on for 500 ms at 0x0808af58 before
     * giving up and tearing the codec down.
     *
     * We never wired line 18: our VIC map came from Rockbox
     * (firmware/export/s5l87xx.h), and Rockbox has a GAP at 18 because it
     * decodes on the ARM and never drives this engine. So the block that would
     * report "buffer consumed" had no way to interrupt at all. See
     * wiki/19 section 11.25.
     */
    qemu_irq irq;

    /*
     * Raise the line as playback advances (SM1_IRQ=1). Off by default: the
     * trigger condition is still being characterised, so the default build
     * stays byte-identical to the measured baseline.
     */
    bool     irq_enabled;
    bool     irq_always;    /* SM1_IRQ=2: pulse regardless of `powered` */
    QEMUTimer *irq_timer;
    uint64_t last_pos;      /* stream position at the previous tick */
    uint64_t irqs;          /* lines raised, for the trace */
    uint64_t ticks;         /* timer ticks, for the idle diagnostic */
};

/* Give SM1 the I2S whose playback its position registers track. */
void ipod_classic_sm1_set_i2s(IPodClassicSM1State *s,
                              struct IPodClassicI2SState *i2s);

#endif
