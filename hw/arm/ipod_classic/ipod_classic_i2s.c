/*
 * S5L8702 I2S transmitter, 0x3CA00000.
 *
 * See include/hw/arm/ipod_classic/ipod_classic_i2s.h for the register map and
 * wiki/14-classic6g-audio.md for the whole audio path and how it was measured.
 *
 * WHAT THIS MODEL DOES
 *
 *  - A register file with honest readback. The retailOS writes CLKCON=1,
 *    TXCON=0x0b100019, TXCOM=0x6 (start), RXCON=0x1000, CLKDIV=272 and reads
 *    I2STXCOM back to confirm the transmitter started; while this block was
 *    create_unimplemented_device every one of those readbacks was zero.
 *
 *  - A TX FIFO sink at +0x10. DMAC0 (a PL080) streams 16-bit PCM samples here.
 *    While the block was unimplemented ~6144 samples per boot fell to the floor.
 *
 *  - PCM capture. With AUDIOWAV=/path.wav set, every sample written to the FIFO
 *    is appended to a 16-bit-stereo WAV whose sample rate is taken from CLKDIV.
 *    That WAV is the hito-3 artefact.
 *
 *  - Pacing (hito 2), when armed. See ipod_classic_i2s_drain(): the FIFO is
 *    drained at the sample rate by a QEMUTimer that gates DMAC0's request line,
 *    so the PL080 hands over one burst at a time and its terminal-count IRQ (16)
 *    reaches the firmware at real playback tempo instead of all at once. This is
 *    what makes the Now Playing progress bar advance with the clock.
 *
 * INSTRUMENTATION: I2STRACE=1 prints every access with its value and guest PC,
 * same pattern as DISPTRACE=1/ATATRACE=1. Independently of the trace, the FIRST
 * non-zero PCM sample of a run is announced on stderr -- that is the event the
 * whole audio investigation waited for.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "hw/core/cpu.h"
#include "hw/arm/ipod_classic/ipod_classic_i2s.h"

/* ------------------------------------------------------------- tracing */

static int i2strace = -1;

static bool i2s_trace_on(void)
{
    if (i2strace < 0) {
        const char *e = getenv("I2STRACE");
        i2strace = (e && *e && *e != '0') ? 1 : 0;
    }
    return i2strace == 1;
}

static uint32_t i2s_trace_pc(void)
{
    CPUState *cs = current_cpu;
    return cs ? (uint32_t)CPU_GET_CLASS(cs)->get_pc(cs) : 0;
}

static const char *i2s_reg_name(hwaddr off)
{
    switch (off) {
    case S5L8702_I2S_CLKCON: return "CLKCON";
    case S5L8702_I2S_TXCON:  return "TXCON";
    case S5L8702_I2S_TXCOM:  return "TXCOM";
    case S5L8702_I2S_TXDB0:  return "TXDB0";
    case S5L8702_I2S_RXCON:  return "RXCON";
    case S5L8702_I2S_RXCOM:  return "RXCOM";
    case S5L8702_I2S_RXDB:   return "RXDB";
    case S5L8702_I2S_STATUS: return "STATUS";
    case S5L8702_I2S_CLKDIV: return "CLKDIV";
    default:                 return "";
    }
}

/* --------------------------------------------------------- WAV capture */

/*
 * One WAV file for the run, opened lazily on the first FIFO write. A file-scope
 * pointer lets the atexit handler patch the RIFF/data sizes back into the
 * header; there is only ever one I2S instance, so a static is enough (same shape
 * as DISPTRACE's atexit totals).
 */
static FILE *i2s_wav;
static uint32_t i2s_wav_data_bytes;
static bool i2s_wav_announced;

static void i2s_wav_put_le32(uint8_t *p, uint32_t v)
{
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

/*
 * Snap 12e6/CLKDIV to the nearest standard sample rate for the header. The PCM
 * samples themselves are stored verbatim, so an off-by-17-Hz label (272 gives
 * 44117, snapped to 44100) never touches the audio. CONVENIENCE: the 32 kHz
 * case, which the SoC clocks as 48 kHz with MCLK dropped to 8 MHz, is labelled
 * 48000 here because this helper assumes a 12 MHz MCLK; no test uses it.
 */
static uint32_t i2s_wav_rate_from_div(uint32_t clkdiv)
{
    static const uint32_t std[] = {
        8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000
    };
    uint32_t hz, best = 44100, bestd = 0xffffffff;

    if (clkdiv == 0) {
        return 44100;
    }
    hz = S5L8702_I2S_MCLK_HZ / clkdiv;
    for (size_t i = 0; i < ARRAY_SIZE(std); i++) {
        uint32_t d = hz > std[i] ? hz - std[i] : std[i] - hz;
        if (d < bestd) {
            bestd = d;
            best = std[i];
        }
    }
    return best;
}

static void i2s_wav_close(void)
{
    uint8_t hdr[8];

    if (!i2s_wav) {
        return;
    }
    /* Patch RIFF chunk size (+4) and data chunk size (+40). */
    if (fseek(i2s_wav, 4, SEEK_SET) == 0) {
        i2s_wav_put_le32(hdr, 36 + i2s_wav_data_bytes);
        fwrite(hdr, 1, 4, i2s_wav);
    }
    if (fseek(i2s_wav, 40, SEEK_SET) == 0) {
        i2s_wav_put_le32(hdr, i2s_wav_data_bytes);
        fwrite(hdr, 1, 4, i2s_wav);
    }
    fclose(i2s_wav);
    i2s_wav = NULL;
    fprintf(stderr, "AUDIOWAV: wrote %u PCM bytes (%u stereo frames)\n",
            i2s_wav_data_bytes, i2s_wav_data_bytes / 4);
}

static void i2s_wav_open(uint32_t rate)
{
    const char *path = getenv("AUDIOWAV");
    uint8_t h[44];
    uint32_t byte_rate = rate * 2 /*chan*/ * 2 /*bytes*/;

    if (!path || !*path) {
        return;
    }
    i2s_wav = fopen(path, "wb");
    if (!i2s_wav) {
        fprintf(stderr, "AUDIOWAV: cannot open %s\n", path);
        return;
    }
    /* Canonical 44-byte PCM WAV header; sizes patched at close. */
    memcpy(h + 0,  "RIFF", 4);
    i2s_wav_put_le32(h + 4, 36);            /* patched */
    memcpy(h + 8,  "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);
    i2s_wav_put_le32(h + 16, 16);           /* fmt chunk size */
    h[20] = 1; h[21] = 0;                   /* PCM */
    h[22] = 2; h[23] = 0;                   /* stereo */
    i2s_wav_put_le32(h + 24, rate);
    i2s_wav_put_le32(h + 28, byte_rate);
    h[32] = 4; h[33] = 0;                   /* block align = 2ch * 2 bytes */
    h[34] = 16; h[35] = 0;                  /* bits per sample */
    memcpy(h + 36, "data", 4);
    i2s_wav_put_le32(h + 40, 0);            /* patched */
    fwrite(h, 1, sizeof(h), i2s_wav);
    i2s_wav_data_bytes = 0;
    atexit(i2s_wav_close);
    fprintf(stderr, "AUDIOWAV: capturing %u Hz 16-bit stereo to %s\n",
            rate, path);
}

/* Append the low `size` bytes of a FIFO write to the WAV, little-endian. */
static void i2s_wav_sample(IPodClassicI2SState *s, uint64_t value, unsigned size)
{
    uint8_t b[4];

    if (getenv("AUDIOWAV") == NULL) {
        return;
    }
    if (!i2s_wav) {
        i2s_wav_open(i2s_wav_rate_from_div(s->clkdiv));
        if (!i2s_wav) {
            return;
        }
    }
    if (size > 4) {
        size = 4;
    }
    i2s_wav_put_le32(b, (uint32_t)value);
    fwrite(b, 1, size, i2s_wav);
    i2s_wav_data_bytes += size;

    if (!i2s_wav_announced && value != 0) {
        i2s_wav_announced = true;
        fprintf(stderr, "s5l8702.i2s: FIRST NON-ZERO PCM sample 0x%0*x "
                "(pc~0x%08x) -- real audio is reaching the FIFO\n",
                size * 2, (uint32_t)value, i2s_trace_pc());
    }
}

/* -------------------------------------------------------------- pacing */

/*
 * Hito 2. Draining the audio DMA at the sample rate so the DMAC's terminal-count
 * IRQ (16) reaches the firmware at real playback tempo instead of all at once,
 * which is what makes a progress bar advance with the clock rather than jump to
 * the end. Gated by AUDIOPACE=1 because it changes the timing of the audio DMA
 * and, until a playback path that produces non-silence exists (the retailOS
 * decodes on the unmodelled CalmADM2E DSP, see wiki/14 §4), its only observable
 * effect is on the boot silence push -- so it is off by default and the default
 * build is byte-for-byte the old behaviour.
 */
static int audiopace = -1;

static bool i2s_pace_on(void)
{
    if (audiopace < 0) {
        const char *e = getenv("AUDIOPACE");
        audiopace = (e && *e && *e != '0') ? 1 : 0;
    }
    return audiopace == 1;
}

static uint32_t i2s_sample_rate(IPodClassicI2SState *s)
{
    return s->clkdiv ? S5L8702_I2S_MCLK_HZ / s->clkdiv : 44100;
}

/*
 * One drain tick: let DMAC0 hand over one chunk to the FIFO, then schedule the
 * next tick after the wall-clock time that chunk represents at the sample rate.
 * Counting the samples the chunk actually moved (tx_samples delta) makes the
 * cadence self-correcting regardless of the chunk size the firmware chose.
 */
static void ipod_classic_i2s_drain(void *opaque)
{
    IPodClassicI2SState *s = opaque;
    uint64_t before, moved;
    int64_t delay_ns;
    uint32_t rate;

    if (!s->tx_running || !s->dmac0) {
        return;
    }
    before = s->tx_samples;
    pl080_pace_request(s->dmac0, S5L8702_I2S_DMA_IIS0_TX);
    moved = s->tx_samples - before;   /* 16-bit samples this chunk */

    rate = i2s_sample_rate(s);
    if (moved >= 2) {
        /* stereo: two 16-bit samples per frame */
        uint64_t frames = moved / 2;
        delay_ns = (int64_t)(frames * 1000000000ULL / rate);
        if (delay_ns < 1000) {
            delay_ns = 1000;
        }
    } else {
        /* Nothing moved: channel not enabled yet or drained. Poll again soon. */
        delay_ns = 2000000;   /* 2 ms */
    }
    if (moved > 0 && i2s_trace_on()) {
        fprintf(stderr, "I2STRACE drain: chunk %" PRIu64 " samples "
                "(%u Hz) -> next tick in %" PRId64 " us\n",
                moved, rate, delay_ns / 1000);
    }
    s->next_tick_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + delay_ns;
    timer_mod(s->drain_timer, s->next_tick_ns);
}

static void i2s_pace_arm(IPodClassicI2SState *s)
{
    if (!i2s_pace_on() || !s->drain_timer) {
        return;
    }
    /* First tick soon; the drain callback then paces itself. */
    timer_mod(s->drain_timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 100000);
}

static void i2s_pace_disarm(IPodClassicI2SState *s)
{
    if (s->drain_timer) {
        timer_del(s->drain_timer);
    }
}

void ipod_classic_i2s_set_dmac0(IPodClassicI2SState *s, PL080State *dmac0)
{
    s->dmac0 = dmac0;
    /*
     * Mark the IIS0_TX request line paced on DMAC0 so mem->peri transfers to the
     * FIFO wait for our drain ticks. Only under AUDIOPACE; otherwise the line is
     * never marked and the PL080 behaves exactly as before.
     */
    if (dmac0 && i2s_pace_on()) {
        pl080_set_paced_line(dmac0, S5L8702_I2S_DMA_IIS0_TX, true);
    }
}

/* MMIO ------------------------------------------------------------------ */

static uint64_t ipod_classic_i2s_read(void *opaque, hwaddr offset,
                                      unsigned size)
{
    IPodClassicI2SState *s = opaque;
    uint32_t val;

    switch (offset) {
    case S5L8702_I2S_CLKCON: val = s->clkcon; break;
    case S5L8702_I2S_TXCON:  val = s->txcon;  break;
    case S5L8702_I2S_TXCOM:  val = s->txcom;  break;
    case S5L8702_I2S_RXCON:  val = s->rxcon;  break;
    case S5L8702_I2S_RXCOM:  val = s->rxcom;  break;
    case S5L8702_I2S_RXDB:   val = s->rxdb;   break;
    case S5L8702_I2S_CLKDIV: val = s->clkdiv; break;
    case S5L8702_I2S_STATUS:
        /*
         * The status register. Its bit meanings are UNDETERMINED -- no source
         * we have documents them and Rockbox never reads it. Returning 0 (what
         * the unimplemented window did) never hung the boot, so we keep 0 as
         * the base and let I2STRACE during a real play tell us which bit, if
         * any, the Now Playing screen waits on. See wiki/14.
         */
        val = 0;
        break;
    default:
        val = 0;
        break;
    }

    if (i2s_trace_on()) {
        fprintf(stderr, "I2STRACE rd +0x%02" HWADDR_PRIx " -> 0x%08x  %-7s "
                "pc~0x%08x\n", offset, val, i2s_reg_name(offset),
                i2s_trace_pc());
    }
    return val;
}

static void ipod_classic_i2s_write(void *opaque, hwaddr offset,
                                   uint64_t value, unsigned size)
{
    IPodClassicI2SState *s = opaque;

    switch (offset) {
    case S5L8702_I2S_CLKCON: s->clkcon = value; break;
    case S5L8702_I2S_TXCON:  s->txcon = value;  break;
    case S5L8702_I2S_TXCOM: {
        bool was_running = s->tx_running;
        s->txcom = value;
        s->tx_running = (value & S5L8702_I2S_TXCOM_RUN) != 0;
        if (s->tx_running && !was_running) {
            i2s_pace_arm(s);
        } else if (!s->tx_running && was_running) {
            i2s_pace_disarm(s);
        }
        break;
    }
    case S5L8702_I2S_RXCON:  s->rxcon = value;  break;
    case S5L8702_I2S_RXCOM:  s->rxcom = value;  break;
    case S5L8702_I2S_CLKDIV: s->clkdiv = value; break;
    case S5L8702_I2S_TXDB0:
        /* A PCM sample from the DMA (or the CPU). Clock it out. */
        s->tx_samples++;
        i2s_wav_sample(s, value, size);
        break;
    default:
        break;
    }

    if (i2s_trace_on() && offset != S5L8702_I2S_TXDB0) {
        /* The FIFO gets thousands of writes a boot; count, do not print. */
        fprintf(stderr, "I2STRACE wr +0x%02" HWADDR_PRIx " = 0x%08x  %-7s "
                "pc~0x%08x\n", offset, (uint32_t)value, i2s_reg_name(offset),
                i2s_trace_pc());
    }
}

static const MemoryRegionOps ipod_classic_i2s_ops = {
    .read = ipod_classic_i2s_read,
    .write = ipod_classic_i2s_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static void ipod_classic_i2s_reset(DeviceState *dev)
{
    IPodClassicI2SState *s = IPOD_CLASSIC_I2S(dev);

    s->clkcon = 0;
    s->txcon = 0;
    s->txcom = 0;
    s->rxcon = 0;
    s->rxcom = 0;
    s->rxdb = 0;
    s->clkdiv = 0;
    s->tx_running = false;
    s->tx_samples = 0;
    if (s->drain_timer) {
        timer_del(s->drain_timer);
    }
}

static void ipod_classic_i2s_realize(DeviceState *dev, Error **errp)
{
    IPodClassicI2SState *s = IPOD_CLASSIC_I2S(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &ipod_classic_i2s_ops, s,
                          TYPE_IPOD_CLASSIC_I2S, S5L8702_I2S_MEM_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);

    s->drain_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                  ipod_classic_i2s_drain, s);
}

static const VMStateDescription vmstate_ipod_classic_i2s = {
    .name = TYPE_IPOD_CLASSIC_I2S,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(clkcon, IPodClassicI2SState),
        VMSTATE_UINT32(txcon, IPodClassicI2SState),
        VMSTATE_UINT32(txcom, IPodClassicI2SState),
        VMSTATE_UINT32(rxcon, IPodClassicI2SState),
        VMSTATE_UINT32(rxcom, IPodClassicI2SState),
        VMSTATE_UINT32(rxdb, IPodClassicI2SState),
        VMSTATE_UINT32(clkdiv, IPodClassicI2SState),
        VMSTATE_BOOL(tx_running, IPodClassicI2SState),
        VMSTATE_END_OF_LIST()
    },
};

static void ipod_classic_i2s_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "S5L8702 I2S transmitter";
    dc->realize = ipod_classic_i2s_realize;
    dc->reset = ipod_classic_i2s_reset;
    dc->vmsd = &vmstate_ipod_classic_i2s;
    dc->user_creatable = false;
}

static const TypeInfo ipod_classic_i2s_info = {
    .name          = TYPE_IPOD_CLASSIC_I2S,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IPodClassicI2SState),
    .class_init    = ipod_classic_i2s_class_init,
};

static void ipod_classic_i2s_register_types(void)
{
    type_register_static(&ipod_classic_i2s_info);
}

type_init(ipod_classic_i2s_register_types)
