/*
 * S5L8702 "SM1" audio clock/stream engine, 0x38500000.
 *
 * See include/hw/arm/ipod_classic/ipod_classic_sm1.h for what the block is and
 * wiki/19-classic6g-dsp-hle.md section 11 for how the map was found. This is the
 * real, stateful replacement for the two hand-faked reads (identity magic + a
 * hard-coded "clock ready" bit) that the previous session dropped into
 * ipod_classic.c. Nothing here is a constant pulled out of thin air: every read
 * that unblocks the guest is derived from a write the guest itself made, so the
 * two bring-up paths in the retailOS -- the codec bring-up 0x0808170c and the
 * start/resume 0x08017378 -- each complete on their own terms instead of racing
 * a timeout or spinning on a zero.
 *
 * WHAT THE GUEST DOES (all VERIFIED in the osos disassembly; roles marked):
 *
 *  - Identity: getter 0x08075d64 reads +0xc80 and the caller compares it to
 *    0x004e7309 (literal *0x080821cc); a mismatch is `b .` forever at 0x080819b8.
 *    VERIFIED role. We answer the magic. (Was the one thing the old stub got.)
 *
 *  - Per-channel sub-blocks: 0x080ba768 drives a small state machine on a
 *    sub-block whose base is base + {idx*0x20 for idx<4; 0x100/0x180/0x200/0x230
 *    for idx 4..7} (computed by 0x080bce04). To power a channel up it writes 4 to
 *    sub+0x10, then polls sub+0x14 (read by 0x080b8470, masked &7) up to ten
 *    times waiting for 7, then writes 16 (run). If the poll never reads 7 it
 *    STILL writes 16 but stamps an error (-236) that the caller can act on. So a
 *    model that leaves sub+0x14 at zero does not hang -- it makes the firmware
 *    believe every channel failed to power up. We make sub+0x14 read 7 once its
 *    sub+0x10 has been commanded, i.e. the channel reached its target state.
 *    VERIFIED offsets/flow; the 4/16/7 meanings are INFERIDO (strong).
 *
 *  - Clock-ready: after the channel and codec config, the bring-up spins on
 *    +0xa98 bit2 with NO timeout (0x08081af8 `tst r0,#4; beq`), and the
 *    start/resume path gates its whole body on the same bit (0x08017394). On
 *    silicon the bit rises when the gated audio clock locks. We raise it once the
 *    firmware has begun powering the engine (its first channel command, the codec
 *    enable at +0x824, the +0x400 start, or the +0xc48 commit). VERIFIED that
 *    bit2 is the awaited bit and that it is un-timed; "clock locked" is INFERIDO.
 *
 *  - Run: the start/resume path writes 1 to +0x400 (0x080ae97c), which itself
 *    re-reads +0x400 expecting exactly 1, and then 0x08017378 polls +0x400 (&3)
 *    until 1 with NO timeout. A plain write latch answers both: write 1, read 1.
 *    VERIFIED both accessors; "run bit" is INFERIDO (strong).
 *
 * Everything else in the low page is a latch (writes stored, reads return the
 * stored word, zero at reset) -- the config strobes at +0x8xx/+0x9xx, +0xa8c,
 * +0xc48, and the status words we have not decoded (+0x404, +0x860, +0xb18,
 * +0x1e0) which stay zero because nothing has been measured to want otherwise.
 * The retailOS never touches +0x1000 (Rockbox's conjectured SM1_DIV), so the
 * upper window is answered as zero exactly like the unimplemented device.
 *
 * This device only ever affects the retailOS audio-clock bring-up. Rockbox never
 * reads SM1, and boot/navigation never enter the bring-up, so both are byte-for-
 * byte unchanged (the block was a zero window for them and, outside +0xc80/
 * +0xa98/+0x400/sub-state, still reads zero).
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "migration/vmstate.h"
#include "hw/core/cpu.h"
#include "hw/irq.h"
#include "qemu/timer.h"
#include "hw/arm/ipod_classic/ipod_classic_sm1.h"
#include "hw/arm/ipod_classic/ipod_classic_hle_audio.h"
#include "hw/arm/ipod_classic/ipod_classic_i2s.h"

/*
 * True and returns the value if `offset` is one of the eight per-channel
 * position registers (+0xa44/+0xa48 for idx 0..3, stride 0x14).
 *
 * +0xa48 is the engine's stream position, read by the audio output path
 * (getter 0x08091710, used by 0x08016e44 and 0x080b3104). We advance it from
 * the I2S transmit-sample count -- the real amount of audio clocked out -- as a
 * stereo-frame count, which is the natural "real clock" reading. MEASURED
 * caveat (wiki/19 section 11): on the reachable play path the copy that would
 * consume this position (0x08016eb4) is skipped by an upstream gate, so the
 * value is presently inconsequential to the audio that is produced; the true
 * wall is further upstream, at the decoder that never fills the mixer's source.
 * It is modelled anyway because it is a real register and a faked constant here
 * would be exactly the kind of thing that stalls a path we have not yet walked.
 */
static bool sm1_pos_reg(IPodClassicSM1State *s, hwaddr offset, uint32_t *out)
{
    for (int idx = 0; idx < S5L8702_SM1_POS_CHANS; idx++) {
        hwaddr a48 = S5L8702_SM1_POS_A48 + idx * S5L8702_SM1_POS_STRIDE;
        hwaddr a44 = S5L8702_SM1_POS_A44 + idx * S5L8702_SM1_POS_STRIDE;
        if (offset != a48 && offset != a44) {
            continue;
        }
        /* Position in stereo frames. When the data-path HLE owns playback, use
         * ITS cursor so pause (frozen), seek (jumped) and track-change (reset)
         * reflect in the position register; otherwise fall back to the I2S
         * transmit-sample count. */
        uint64_t pos;
        if (ipod_classic_hle_audio_have_pcm()) {
            pos = ipod_classic_hle_audio_position_frames();
        } else {
            pos = s->i2s ? s->i2s->tx_samples / 2 : 0;
        }
        /*
         * WRAP THE POSITION INTO THE RING. This is the fix for the wall that
         * fourteen assaults called a "structural deadlock"; see wiki/19 §11.26.
         *
         * MEASURED: the render's output pointer is computed by the firmware as
         *     dest = 0x22020000 + (this position * 4)
         * i.e. IRAM1's base plus the position in stereo frames. Proof: with the
         * position forced to 0 the copy's destination is exactly 0x22020000
         * (IRAM1 base); left free-running it was 0x222dc000 and then 0x222fbc00
         * on a later run -- 0x22020000 plus the audio bytes elapsed since play
         * started.
         *
         * So the engine's position register is a RING OFFSET, not a cumulative
         * total. Reporting the raw count walked the firmware's write pointer out
         * of IRAM1 after ~1.9 s of playback; the stores then hit unmapped memory
         * and took a data abort inside the tiny silence-copy at 0x08085ea0
         * (40 bytes, 10 words), so the copy never returned -- and neither did
         * the mixer 0x0809a74c, the render 0x0808170c, or the pump 0x22006118.
         * That is what looked like a rendezvous deadlock from every angle: the
         * render was not waiting for anything, it was faulting.
         *
         * The ring is IRAM1 (0x22020000, 128 KiB), so the position wraps every
         * 128 KiB / 4 bytes-per-frame = 32768 stereo frames, which keeps every
         * write the firmware issues inside that buffer.
         *
         * SM1_POS overrides the wrap for experiments (0 = pin the position at
         * the ring base).
         */
        {
            static int pos_mode = -1;
            static uint64_t pos_wrap;
            if (pos_mode < 0) {
                const char *e = getenv("SM1_POS");
                pos_wrap = (e && *e) ? strtoull(e, NULL, 0)
                                     : S5L8702_SM1_POS_RING_FRAMES;
                pos_mode = 1;
                if (e && *e) {
                    fprintf(stderr, "SM1_POS: ring wrap overridden to %llu frames\n",
                            (unsigned long long)pos_wrap);
                }
            }
            pos = pos_wrap ? (pos % pos_wrap) : 0;
        }
        /* +0xa48 is the position the copy uses; +0xa44 is its companion,
         * answered 0 until measurement says otherwise. */
        *out = (offset == a48) ? (uint32_t)pos : 0;
        return true;
    }
    return false;
}

void ipod_classic_sm1_set_i2s(IPodClassicSM1State *s,
                              struct IPodClassicI2SState *i2s)
{
    s->i2s = i2s;
}

/* --------------------------------------------------------- interrupt ------ */
/*
 * The engine's "playback advanced" interrupt, VIC0 line 18.
 *
 * On real silicon the audio engine tells the CPU when it has consumed audio;
 * Apple's driver hangs its whole render pump off that report (see the header,
 * and wiki/19 section 11.25). We had no line at all here, so the report could
 * never arrive. Raise it when the stream position has moved since the last
 * tick -- i.e. when audio really was clocked out -- which is the condition the
 * handler itself checks for (it reads the engine status and tests bit 2, the
 * same bit our `powered` state answers).
 *
 * Level-style pulse: raise, then drop on the next tick, so a handler that does
 * not clear anything still sees discrete events rather than a stuck line.
 * Gated by SM1_IRQ=1; with it unset nothing here runs and the machine behaves
 * exactly as the measured baseline.
 */
#define SM1_IRQ_TICK_MS 5

static uint64_t sm1_stream_pos(IPodClassicSM1State *s)
{
    if (ipod_classic_hle_audio_have_pcm()) {
        return ipod_classic_hle_audio_position_frames();
    }
    return s->i2s ? s->i2s->tx_samples / 2 : 0;
}

/* Drop the engine interrupt line; see the note where it is scheduled. */
static void sm1_irq_off(void *opaque)
{
    IPodClassicSM1State *s = opaque;

    if (s->irq_pending) {
        s->irq_pending = false;
        qemu_irq_lower(s->irq);
    }
}

static void sm1_irq_tick(void *opaque)
{
    IPodClassicSM1State *s = opaque;
    uint64_t pos = sm1_stream_pos(s);

    /*
     * The trigger is the guest's own arming write to +0xa8c (see the write
     * handler). Earlier attempts gated on our derived `powered` flag (never set
     * on the play path) or on the stream position advancing (circular: the
     * position cannot move until the render this interrupt unblocks has run).
     * Both measured raised=0. Arming is the firmware's request, so honouring it
     * is what the hardware would do.
     *
     * Cadence: the audio DMA delivers ~43 bursts/s (2048 stereo frames each,
     * measured), so a frame-done report every ~25 ms is the right order. We tick
     * at 5 ms and fire every fifth tick.
     *
     * Shape: assert with the status bit set, then drop both on the next tick, so
     * the handler sees a discrete event and a missed one cannot wedge the line.
     */
    s->ticks++;
    /*
     * SM1_IRQ_EVERY overrides how many 5 ms ticks pass between reports.
     *
     * Needed to test a specific failure: the audio task's loop at 0x22006324
     * calls the event waiter, and when the waiter returns failure it sleeps and
     * retries -- but only TWICE (`cmp r7,#1 ; bgt exit`) before giving up. The
     * waiter's own wait carries a timeout of 500. So if a frame report does not
     * arrive inside that timeout the whole audio pump bails after two attempts,
     * which is exactly the eight invocations measured. Making the report more
     * frequent says whether the cadence is what misses the timeout.
     */
    static int every = -1;
    if (every < 0) {
        const char *e = getenv("SM1_IRQ_EVERY");
        every = (e && *e) ? atoi(e) : 5;
        if (every < 1) {
            every = 1;
        }
    }
    if (!s->irq_pending && (s->irq_armed || s->irq_always) && (s->ticks % every == 0)) {
        s->irq_pending = true;
        s->last_pos = pos;
        s->irqs++;
        qemu_irq_raise(s->irq);
        /*
         * The line is a LEVEL, but a short one.
         *
         * Holding it until the next 5 ms tick produced the storm in s11.41:
         * 4 raises, 789 182 handler entries even after any block read became an
         * acknowledge. That is ~197 000 re-entries inside one 5 ms window, which
         * is only possible because virtual time barely advances while the CPU
         * spins in the interrupt loop -- so a coarse timer is exactly the wrong
         * thing to be holding an interrupt line with.
         *
         * MEASURED, a bare pulse does not work: with SM1_IRQ_PULSE=1 (raise and
         * lower in the same instant) the handler count went to ZERO and so did
         * creditsig. The VIC wants a level; an edge is lost. So the line has to
         * stay up long enough to be dispatched and no longer.
         *
         * SM1_IRQ_ASSERT_US is that window, and it is a MODEL CHOICE, not a fact
         * about the part: what the real engine's line does is UNDETERMINED, and
         * what the firmware touches to acknowledge it has not been found -- the
         * handler's status getter at 0x080a7638 does not appear to reach this
         * block, or broadening the read acknowledge would have ended the storm
         * instead of trimming it by a third.
         */
        timer_mod(s->irq_off_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                  (int64_t)S5L8702_SM1_IRQ_ASSERT_US * 1000);
        if (s->irqs <= 4 || s->irqs % 2000 == 0) {
            fprintf(stderr, "SM1_IRQ: line 18 raised #%" PRIu64
                    " (armed=%d powered=%d pos=%" PRIu64 ")\n",
                    s->irqs, (int)s->irq_armed, (int)s->powered, pos);
        }
    }
    timer_mod(s->irq_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
              (int64_t)SM1_IRQ_TICK_MS * 1000000);
}

/*
 * SM1TRACE=1 prints every access with its value and the guest PC, the same
 * pattern as I2STRACE/ATATRACE/DISPTRACE. SM1 is touched only during the audio
 * bring-up (a few dozen accesses), so this is cheap and stays in as the way to
 * watch the handshake -- e.g. to confirm which bring-up path (0x0808170c codec
 * bring-up vs 0x08017378 start/resume) runs and in what order.
 */
static int sm1trace = -1;

static bool sm1_trace_on(void)
{
    if (sm1trace < 0) {
        const char *e = getenv("SM1TRACE");
        sm1trace = (e && *e && *e != '0') ? 1 : 0;
    }
    return sm1trace == 1;
}

/*
 * SM1HI=1: report writes ABOVE the modelled 4 KB register page.
 *
 * Its own variable on purpose. The question -- does the firmware put anything
 * in the other 1020 KB of this block's window? -- needs a handful of lines,
 * while SM1TRACE prints one line per register access and produced a log big
 * enough to take the whole WSL filesystem down with it. Cheap when unset.
 */
static int sm1hi = -1;

static bool sm1_hi_on(void)
{
    if (sm1hi < 0) {
        const char *e = getenv("SM1HI");
        sm1hi = (e && *e && *e != '0') ? 1 : 0;
    }
    return sm1hi == 1;
}

static uint32_t sm1_trace_pc(void)
{
    CPUState *cs = current_cpu;
    return cs ? (uint32_t)CPU_GET_CLASS(cs)->get_pc(cs) : 0;
}

/*
 * The eight sub-block "state" registers (sub+0x14). Their absolute offsets are
 * the sub-block bases {0,0x20,0x40,0x60,0x100,0x180,0x200,0x230} plus 0x14. Each
 * one's command register (sub+0x10) sits exactly 4 bytes below it. We enumerate
 * them rather than test "offset ends in 4" so we never mistake an unrelated
 * register (e.g. +0x404, whose 4-below neighbour is the +0x400 run register) for
 * a sub-block state.
 */
static const hwaddr sm1_sub_state_off[] = {
    0x0014, 0x0034, 0x0054, 0x0074,   /* idx 0..3: base + idx*0x20 + 0x14 */
    0x0114, 0x0194, 0x0214, 0x0244,   /* idx 4..7                          */
};

static bool sm1_is_sub_state(hwaddr offset)
{
    for (size_t i = 0; i < ARRAY_SIZE(sm1_sub_state_off); i++) {
        if (offset == sm1_sub_state_off[i]) {
            return true;
        }
    }
    return false;
}

static bool sm1_is_sub_cmd(hwaddr offset)
{
    /* A command register is the state register minus 4. */
    for (size_t i = 0; i < ARRAY_SIZE(sm1_sub_state_off); i++) {
        if (offset == sm1_sub_state_off[i] - 4) {
            return true;
        }
    }
    return false;
}

static uint64_t ipod_classic_sm1_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    IPodClassicSM1State *s = opaque;
    uint32_t val;
    bool was_pending = s->irq_pending;

    /*
     * ANY read of this block acknowledges the engine interrupt.
     *
     * It used to be only +0xa98, on the reasoning that the line-18 handler's
     * status getter reads exactly that. MEASURED, that is not enough: in one run
     * the model raised the line FOUR times and the handler at 0x08017378 was
     * entered 1,196,485 times -- and QEMU's own -d int log counted ~1.05 million
     * IRQ exceptions. The line stays asserted for the whole 5 ms until the next
     * tick drops it, and the guest re-enters the handler for that entire window,
     * which starves every audio thread. That is why the decode side crawled:
     * dpump ran 8 times and creditsig 493 while the handler ran 1.2 million.
     *
     * Whatever the getter at 0x080a7638 actually touches, the handler does reach
     * this block, so taking any read as the acknowledge cannot leave the level
     * wedged. The STAT value below still reports bit 2 from the pre-clear state,
     * so the handler that triggered on this interrupt still sees it set and
     * signals its event.
     */
    if (was_pending) {
        s->irq_pending = false;
        qemu_irq_lower(s->irq);
    }

    /* Above the modelled low page: a plain zero window, as before. */
    if (offset >= S5L8702_SM1_REG_BYTES) {
        return 0;
    }

    switch (offset) {
    case S5L8702_SM1_ID:
        /* Block identity. A mismatch here is an eternal `b .`. */
        val = S5L8702_SM1_ID_MAGIC;
        break;

    case S5L8702_SM1_STAT:
        /*
         * Clock/engine status, read by getter 0x080a7638 (r1=0 -> [base+0xa98],
         * top five bits masked off by the firmware itself).
         *
         * Bit 2 serves two readers:
         *  - the bring-up spin at 0x08081af8 ("clock ready"), answered from
         *    `powered` as before; and
         *  - the VIC line-18 handler 0x08017378, which does `tst r0,#4` and only
         *    signals the render-credit event 0x22008c58 when the bit is set. So
         *    while we are asserting the engine interrupt the bit must read set,
         *    or the handler would take its early exit and signal nothing.
         */
        val = (s->powered || was_pending) ? S5L8702_SM1_STAT_READY : 0;
        /*
         * SM1_FRAME_BIT=1 also reports bit 12 while a frame report is pending.
         *
         * The waiter on the render event (0x0808af58) reads this same word and
         * does `tst r0,#0x1000` before doing its per-frame work, so with only
         * bit 2 answered that branch is never taken -- which looked like the
         * missing piece. MEASURED, it is not: with the bit set, dpump went 16 ->
         * 14 and aacmath stayed at 1. So bit 12 means something else, and it is
         * left OFF by default rather than inventing a status the part may never
         * report. Kept as a switch because the finding -- that this waiter gates
         * on bit 12 while the interrupt handler gates on bit 2 -- is real and
         * worth being able to re-test.
         */
        if (was_pending && getenv("SM1_FRAME_BIT")) {
            val |= S5L8702_SM1_STAT_FRAME;
        }
        /*
         * The acknowledge itself happens at the top of this function, for ANY
         * offset -- see the note there for why narrowing it to this register was
         * measured insufficient. `was_pending` is the pre-clear state, so a
         * handler that got here because of this interrupt still reads bit 2 set.
         */
        break;

    case S5L8702_SM1_RUN:
        /*
         * Run/run-state. The start path writes 1 here and both it and the
         * writer's own retry loop read the value straight back, so return the
         * latched word (write 1 -> read 1). The &3 the poll applies is the
         * firmware's, not ours.
         */
        val = s->reg[S5L8702_SM1_RUN / 4];
        break;

    default:
        if (sm1_is_sub_state(offset)) {
            /*
             * Sub-block state (sub+0x14): a STATE MACHINE, not a sticky latch.
             *
             * It reports "reached target state" (7) while the channel is
             * commanded up or running, and returns to 0 (idle) once it has been
             * commanded to stop. Two firmware sites read it and they want
             * OPPOSITE values, which is what forces the distinction:
             *
             *  - The power-up 0x080ba768 writes cmd 4, polls for 7 up to ten
             *    times, writes cmd 16 (run) -- and finally writes cmd 96 (stop).
             *  - The render 0x0808170c then reads sub-block 4's state at
             *    0x080820f0 and REQUIRES 0:
             *        80820f0: bl   0x80b8470     ; read sub+0x14, & 7
             *        80820f4: cmp  r0, #0
             *        80820f8: bne  0x80821a8     ; non-zero -> SKIP the build
             *    The block it skips, 0x08082100-0x080821a0, is the engine
             *    CONTEXT CONSTRUCTION: the buffer copy, the descriptor
             *    allocation (0x080b67a8), the chain append (0x080bfa58) and the
             *    address translation (0x08075d80).
             *
             * Answering a sticky 7 satisfied the power-up poll but made the
             * render skip its own context build forever -- which is exactly the
             * "missing render-context construction step" wiki/19 s11.25 was
             * looking for, and why every gate forced downstream revealed the
             * layer below it was also unbuilt.
             *
             * So: 7 while the last command is up (4) or run (16); 0 once the
             * channel has been stopped (96) or never commanded.
             */
            uint32_t cmd = s->reg[(offset - 4) / 4];
            switch (cmd) {
            case S5L8702_SM1_SUB_CMD_UP:
            case S5L8702_SM1_SUB_CMD_RUN:
                val = S5L8702_SM1_SUB_STATE_READY;
                break;
            default:            /* 0 = never commanded, 96 = stopped */
                val = 0;
                break;
            }
        } else if (sm1_pos_reg(s, offset, &val)) {
            /* Per-channel playback position; advances with I2S playback. */
        } else {
            /* Latch: config strobes and undecoded status words read back as
             * stored (zero at reset). */
            val = s->reg[offset / 4];
        }
        break;
    }

    if (sm1_trace_on()) {
        fprintf(stderr, "SM1TRACE rd +0x%03" HWADDR_PRIx " -> 0x%08x  "
                "pc~0x%08x%s\n", offset, val, sm1_trace_pc(),
                offset == S5L8702_SM1_STAT ? "  [STAT bit2=ready]" :
                offset == S5L8702_SM1_ID ? "  [ID]" :
                offset == S5L8702_SM1_RUN ? "  [RUN]" :
                sm1_is_sub_state(offset) ? "  [SUBSTATE]" : "");
    }
    return val;
}

static void ipod_classic_sm1_write(void *opaque, hwaddr offset,
                                   uint64_t value, unsigned size)
{
    IPodClassicSM1State *s = opaque;

    if (offset >= S5L8702_SM1_REG_BYTES) {
        /*
         * A BLIND SPOT worth naming. The block's window is 1 MB and only its
         * first 4 KB is modelled; everything above was dropped here silently,
         * BEFORE the trace below, so SM1TRACE could never show it either. If
         * the firmware puts descriptors -- or DSP code -- up there, the whole
         * lot has been going into a hole and no measurement in this
         * investigation would have noticed.
         *
         * Counting is enough to answer that, and costs nothing when the trace
         * is off. Doubling schedule so a bulk upload is loud and a stray write
         * stays quiet.
         */
        if (sm1_hi_on()) {
            static uint64_t hi_writes;
            static hwaddr hi_lo = (hwaddr)-1, hi_hi;
            uint64_t n = ++hi_writes;

            if (offset < hi_lo) {
                hi_lo = offset;
            }
            if (offset > hi_hi) {
                hi_hi = offset;
            }
            if ((n & (n - 1)) == 0 || n % 100000 == 0) {
                fprintf(stderr, "SM1TRACE HI-WRITE #%" PRIu64 " +0x%05"
                        HWADDR_PRIx " = 0x%08x  (rango visto +0x%05" HWADDR_PRIx
                        "..+0x%05" HWADDR_PRIx ")  pc~0x%08x\n",
                        n, offset, (uint32_t)value, hi_lo, hi_hi,
                        sm1_trace_pc());
            }
        }
        return;
    }

    /*
     * The engine is "powered" (clock will lock, run bit will follow) once the
     * firmware issues any of its bring-up writes. This is the single derived
     * signal behind +0xa98 bit2; deriving it from the guest's own first move
     * keeps the model coherent instead of hard-wiring the bit on. VERIFIED that
     * each of these writes is part of a bring-up; that they are what "powers"
     * the clock is INFERIDO.
     */
    switch (offset) {
    case S5L8702_SM1_CODEC_EN:   /* +0x824 codec enable   */
    case S5L8702_SM1_COMMIT:     /* +0xc48 config commit  */
        s->powered = true;
        break;
    case S5L8702_SM1_RUN:        /* +0x400 start (nonzero) */
        if (value & S5L8702_SM1_RUN_MASK) {
            s->powered = true;
        }
        break;
    case S5L8702_SM1_INTEN:      /* +0xa8c interrupt enable */
        /*
         * The engine's interrupt enable. The firmware arms it from a four-
         * instruction leaf that writes the bare constant:
         *
         *   08017364: ldr r1, =0x38500000 ; mov r0, #4 ; str r0, [r1, #0xa8c]
         *
         * called at 0x080820e4 from the render's own path. Bit 2 here pairs with
         * bit 2 of the status at +0xa98, which the VIC line-18 handler
         * (0x08017378) tests before signalling the render-credit event
         * 0x22008c58. In other words the firmware is asking the engine to
         * interrupt it when a frame is done -- and then waits for exactly that.
         *
         * We used to keep +0xa8c as a plain latch and had no interrupt line at
         * all, so the request could never be answered: the render pump's 500 ms
         * wait (0x0808af58) always timed out and tore the codec down. Arming
         * here makes the trigger the guest's own, rather than a timer we chose.
         */
        if (value & S5L8702_SM1_INTEN_FRAME) {
            if (!s->irq_armed) {
                s->irq_armed = true;
                fprintf(stderr, "SM1_IRQ: guest armed the engine interrupt "
                        "(+0xa8c = 0x%02x); VIC line 18 live\n",
                        (uint32_t)value);
            }
        } else {
            s->irq_armed = false;
        }
        break;
    default:
        if (sm1_is_sub_cmd(offset) && value != 0) {
            s->powered = true;   /* a channel was commanded up */
        }
        break;
    }

    s->reg[offset / 4] = (uint32_t)value;

    if (sm1_trace_on()) {
        fprintf(stderr, "SM1TRACE wr +0x%03" HWADDR_PRIx " = 0x%08x  "
                "pc~0x%08x%s%s\n", offset, (uint32_t)value, sm1_trace_pc(),
                sm1_is_sub_cmd(offset) ? "  [SUBCMD]" : "",
                s->powered ? "  powered" : "");
    }
}

static const MemoryRegionOps ipod_classic_sm1_ops = {
    .read = ipod_classic_sm1_read,
    .write = ipod_classic_sm1_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void ipod_classic_sm1_reset(DeviceState *dev)
{
    IPodClassicSM1State *s = IPOD_CLASSIC_SM1(dev);

    memset(s->reg, 0, sizeof(s->reg));
    s->powered = false;
}

static void ipod_classic_sm1_init(Object *obj)
{
    IPodClassicSM1State *s = IPOD_CLASSIC_SM1(obj);

    memory_region_init_io(&s->iomem, obj, &ipod_classic_sm1_ops, s,
                          TYPE_IPOD_CLASSIC_SM1, S5L8702_SM1_MEM_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);

    /*
     * The engine's interrupt line. Always created so the machine can wire it to
     * VIC0 line 18; only ever RAISED when SM1_IRQ=1, so the default build is
     * byte-identical to the baseline every prior session measured.
     */
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    const char *e = getenv("SM1_IRQ");
    s->irq_enabled = e && *e && *e != '0';
    s->irq_always = e && *e == '2';
    if (s->irq_enabled) {
        s->irq_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, sm1_irq_tick, s);
        s->irq_off_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, sm1_irq_off, s);
        timer_mod(s->irq_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                  (int64_t)SM1_IRQ_TICK_MS * 1000000);
        fprintf(stderr, "SM1_IRQ: engine interrupt armed on VIC0 line 18\n");
    }
}

static const VMStateDescription vmstate_ipod_classic_sm1 = {
    .name = TYPE_IPOD_CLASSIC_SM1,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(reg, IPodClassicSM1State, S5L8702_SM1_REG_WORDS),
        VMSTATE_BOOL(powered, IPodClassicSM1State),
        VMSTATE_END_OF_LIST()
    },
};

static void ipod_classic_sm1_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "S5L8702 SM1 audio clock/stream engine";
    dc->reset = ipod_classic_sm1_reset;
    dc->vmsd = &vmstate_ipod_classic_sm1;
    dc->user_creatable = false;
}

static const TypeInfo ipod_classic_sm1_info = {
    .name          = TYPE_IPOD_CLASSIC_SM1,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IPodClassicSM1State),
    .instance_init = ipod_classic_sm1_init,
    .class_init    = ipod_classic_sm1_class_init,
};

static void ipod_classic_sm1_register_types(void)
{
    type_register_static(&ipod_classic_sm1_info);
}

type_init(ipod_classic_sm1_register_types)
