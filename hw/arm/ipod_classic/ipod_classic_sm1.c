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
         * Clock/engine status. Only bit2 (clock ready) is decoded; it rises once
         * the firmware has begun bringing the engine up. Everything else stays 0
         * (the firmware masks off the top five bits itself). INFERIDO.
         */
        val = s->powered ? S5L8702_SM1_STAT_READY : 0;
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
             * Sub-block state (sub+0x14). Reads 7 ("reached target state") once
             * its command register (sub+0x10, four bytes below) has been written
             * a non-zero command; 0 before that. This is what the 10-try
             * power-up poll in 0x080ba768 waits for. INFERIDO (strong).
             */
            uint32_t cmd = s->reg[(offset - 4) / 4];
            val = cmd ? S5L8702_SM1_SUB_STATE_READY : 0;
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
