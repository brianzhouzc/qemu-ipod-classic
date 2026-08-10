/*
 * S5L8702 display pipe, 0x38900000.
 *
 * See include/hw/arm/ipod_classic/ipod_classic_disp.h for the register map and
 * where every line of it comes from. In short: this is the block that holds the
 * panel geometry, the three layer descriptors, the source-address slots and the
 * gamma tables, and it is what the LCD frame engine (LCD +0x70) presents.
 *
 * The model is a register file with honest readback and a loud trace. That is
 * deliberately less than a composition engine, and the reason is a measurement:
 * a boot that never mounts a volume never writes a source address into any of
 * the nine slots, so there is nothing to compose. Until one arrives, a
 * compositor here would be inventing pixels rather than modelling hardware.
 *
 * Readback alone is not cosmetic. Six functions in the osos do
 * read-modify-write on +0x008 and one on +0x200; while this block was
 * create_unimplemented_device() every one of them read zero and dropped every
 * field written before it. That is the same failure mode the DMAC0 note in
 * wiki/11 describes, and it was silently true here too.
 *
 * DISPTRACE=1 prints every access with its value and the guest PC, and names
 * the register when we know its name. The first write of a source address is
 * printed unconditionally and never throttled: that is the event the whole
 * display investigation is waiting for.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "hw/core/cpu.h"
#include "exec/address-spaces.h"
#include "hw/arm/ipod_classic/ipod_classic_disp.h"

/*
 * INSTRUMENTATION ONLY -- nothing below changes what the model does. Same
 * pattern as ATATRACE=1 and LCDTRACE=1 elsewhere in this machine.
 */
static int disptrace = -1;
static void disp_trace_totals(void);

static bool disp_trace_on(void)
{
    if (disptrace < 0) {
        const char *e = getenv("DISPTRACE");
        disptrace = (e && *e && *e != '0') ? 1 : 0;
        if (disptrace) {
            atexit(disp_trace_totals);
        }
    }
    return disptrace == 1;
}

/*
 * The last PC the CPU synchronised. Under TCG that is the head of the running
 * translation block, not the address of the store -- io_prepare() does not call
 * cpu_restore_state() -- so it is the same granularity as -d in_asm and useful
 * for the same thing: naming the function. Printed as "pc~" to say so.
 */
static uint32_t disp_trace_pc(void)
{
    CPUState *cs = current_cpu;
    return cs ? (uint32_t)CPU_GET_CLASS(cs)->get_pc(cs) : 0;
}

static uint64_t disp_reg_writes;
static uint64_t disp_gamma_writes;
static uint64_t disp_src_writes;
static uint64_t disp_ctrl_reads;

static void disp_trace_totals(void)
{
    fprintf(stderr, "DISPTRACE totals: %" PRIu64 " register writes below "
            "+0x400, %" PRIu64 " gamma-table writes, %" PRIu64 " reads of the "
            "control register +0x008, and %" PRIu64 " writes of a SOURCE "
            "ADDRESS (+0x038..+0x044, +0x060/78/90/a8/c0)\n",
            disp_reg_writes, disp_gamma_writes, disp_ctrl_reads,
            disp_src_writes);
}

/* Is this offset one of the nine source-address slots? See the header. */
static bool disp_is_source(hwaddr off)
{
    if (off >= S5L8702_DISP_SRC_PLANE0 && off <= S5L8702_DISP_SRC_PLANE3) {
        return true;
    }
    for (int i = 0; i < S5L8702_DISP_NSLOT; i++) {
        if (off == S5L8702_DISP_SRC_SLOT0 + i * S5L8702_DISP_SRC_STRIDE) {
            return true;
        }
    }
    return false;
}

static const char *disp_reg_name(hwaddr off)
{
    switch (off) {
    case S5L8702_DISP_CONTROL:    return "CONTROL";
    case S5L8702_DISP_DST_ORIGIN: return "DST_ORIGIN";
    case S5L8702_DISP_DST_LIMIT:  return "DST_LIMIT";
    case 0x038:                   return "SRC_PLANE0";
    case 0x03c:                   return "SRC_PLANE1";
    case 0x040:                   return "SRC_PLANE2";
    case 0x044:                   return "SRC_PLANE3";
    case S5L8702_DISP_DST_SIZE:   return "DST_SIZE";
    case S5L8702_DISP_DST_OFF:    return "DST_OFF";
    case S5L8702_DISP_DST_CLIP:   return "DST_CLIP";
    case 0x060:                   return "SRC_SLOT0";
    case 0x078:                   return "SRC_SLOT1";
    case 0x090:                   return "SRC_SLOT2";
    case 0x0a8:                   return "SRC_SLOT3";
    case 0x0c0:                   return "SRC_SLOT4";
    case 0x0d8:                   return "LAYER0_CFG";
    case 0x0dc:                   return "LAYER0_W1";
    case 0x0e0:                   return "LAYER1_CFG";
    case 0x0e4:                   return "LAYER1_W1";
    case 0x0e8:                   return "LAYER2_CFG";
    case 0x0ec:                   return "LAYER2_W1";
    case S5L8702_DISP_STATUS200:  return "STATUS200";
    case S5L8702_DISP_GEOMETRY:   return "GEOMETRY";
    case S5L8702_DISP_FORMAT:     return "FORMAT";
    default:                      return NULL;
    }
}

static uint64_t ipod_classic_disp_read(void *opaque, hwaddr offset,
                                       unsigned size)
{
    IPodClassicDispState *s = opaque;
    uint32_t val = s->reg[offset >> 2];

    if (offset == S5L8702_DISP_CONTROL) {
        disp_ctrl_reads++;
    }

    if (disp_trace_on()) {
        const char *n = disp_reg_name(offset);
        fprintf(stderr, "DISPTRACE rd +0x%03" HWADDR_PRIx " -> 0x%08x  %-10s "
                "pc~0x%08x\n", offset, val, n ? n : "", disp_trace_pc());
    }
    return val;
}

static void ipod_classic_disp_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned size)
{
    IPodClassicDispState *s = opaque;
    bool gamma = offset >= S5L8702_DISP_GAMMA0;

    s->reg[offset >> 2] = value;

    if (gamma) {
        disp_gamma_writes++;
    } else {
        disp_reg_writes++;
    }

    /*
     * The one event worth shouting about, trace or no trace: a source address.
     * Every boot measured so far writes zero of these, which is exactly why
     * nothing appears on the panel. If this line ever prints, the display
     * investigation has a new fact. Printed at most once per distinct value so
     * a running UI cannot flood stderr -- the channel the firmware itself
     * talks on (lesson 1 of wiki/09).
     */
    if (disp_is_source(offset) && value != 0) {
        static uint32_t last_src;
        if ((uint32_t)value != last_src) {
            last_src = value;
            disp_src_writes++;
            fprintf(stderr, "s5l8702.disp: SOURCE ADDRESS +0x%03" HWADDR_PRIx
                    " = 0x%08x  (pc~0x%08x) -- first time this block has been "
                    "given something to read from\n",
                    offset, (uint32_t)value, disp_trace_pc());
        }
        return;
    }

    if (disp_trace_on()) {
        const char *n = disp_reg_name(offset);
        if (gamma) {
            /* 768 of these per boot; the total is printed at exit. */
            return;
        }
        fprintf(stderr, "DISPTRACE wr +0x%03" HWADDR_PRIx " = 0x%08x  %-10s "
                "pc~0x%08x\n", offset, (uint32_t)value, n ? n : "",
                disp_trace_pc());
    }
}

/* --------------------------------------------------------------- compose */

/*
 * Bytes per pixel by format code. From the switch at 0x08126f90, which turns
 * the format field into a multiplier r1 and then computes the source stride as
 * r1 * srcW / 2 -- so r1 is twice the bytes per pixel. VERIFIED against both
 * layers a real boot programs: fmt 7 with stride 1280 for a 320-wide surface
 * (4 bytes) and fmt 3 with stride 640 (2 bytes).
 */
static unsigned disp_bpp(unsigned fmt)
{
    switch (fmt) {
    case 0:  return 0;   /* 4 bpp; never seen, and a half-byte pixel needs its
                          * own path. Skipped and logged rather than guessed. */
    case 1:  return 1;   /* 8 bpp, presumably paletted. Never seen. */
    case 2: case 3: case 4: case 5: return 2;
    case 6: case 7:                 return 4;
    default: return 0;
    }
}

static inline uint16_t disp_rgb565(unsigned r, unsigned g, unsigned b)
{
    return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
}

/*
 * Draw one layer. Returns true if any pixel landed on the panel.
 *
 * PIXEL ORDER, and this is the one INFERRED thing in the whole path. A 32-bit
 * source word is read as little-endian ARGB8888, i.e. the bytes in memory are
 * B, G, R, A; a 16-bit one as RGB565. Two reasons, and neither is a datasheet:
 *   - those are the standard readings of both formats, and Apple's graphics
 *     stack uses ARGB8888 throughout;
 *   - the selection bar on the language screen comes out 0xff0484da, which
 *     under that reading is #0484da, Apple's iPod selection blue. Read the
 *     other way round it is #da8404, an orange that appears nowhere in this
 *     user interface.
 * If better evidence turns up, it is two lines below and nothing else changes.
 */
static bool disp_draw_layer(IPodClassicDispState *s, int k, uint16_t *fb,
                            unsigned width, unsigned height)
{
    hwaddr base = S5L8702_DISP_SLOT_BASE + k * S5L8702_DISP_SLOT_STEP;
    uint32_t stride = s->reg[(base + S5L8702_DISP_SLOT_STRIDE) >> 2];
    uint32_t fmtw   = s->reg[(base + S5L8702_DISP_SLOT_FORMAT) >> 2];
    uint32_t src    = s->reg[(base + S5L8702_DISP_SLOT_SRC) >> 2];
    uint32_t sizew  = s->reg[(base + S5L8702_DISP_SLOT_SIZE) >> 2];
    uint32_t posw   = s->reg[(base + S5L8702_DISP_SLOT_POS) >> 2];
    unsigned fmt = (fmtw >> 8) & 0xff;
    unsigned bpp = disp_bpp(fmt);
    /* Signed: a pane sliding off the right edge is written with width -55. */
    int w = (int16_t)(sizew >> 16), h = (int16_t)(sizew & 0xffff);
    int x = (int16_t)(posw >> 16),  y = (int16_t)(posw & 0xffff);

    if (src == 0 || w <= 0 || h <= 0) {
        return false;
    }
    if (bpp != 2 && bpp != 4) {
        /*
         * 4- and 8-bit formats need a palette we have never seen written, so
         * there is nothing to decode them with. No boot has ever programmed
         * one; if this fires, that has changed and it is worth knowing.
         */
        qemu_log_mask(LOG_UNIMP, "s5l8702.disp: layer %d has pixel format %u "
                      "(%u bytes per pixel), which this model does not decode; "
                      "skipped\n", k, fmt, bpp);
        return false;
    }
    if (x >= (int)width || y >= (int)height || x + w <= 0 || y + h <= 0) {
        return false;
    }

    /* Clip to the panel. Left/top clipping also has to advance the source. */
    if (x < 0) {
        src += (unsigned)(-x) * bpp;
        w += x;
        x = 0;
    }
    if (y < 0) {
        src += (unsigned)(-y) * stride;
        h += y;
        y = 0;
    }
    if (x + w > (int)width) {
        w = width - x;
    }
    if (y + h > (int)height) {
        h = height - y;
    }
    /* The row scratch is one panel-width of 32-bit pixels; never exceed it. */
    if ((unsigned)w * bpp > sizeof(s->rowbuf)) {
        w = sizeof(s->rowbuf) / bpp;
    }

    for (int row = 0; row < h; row++) {
        uint16_t *dst = fb + (y + row) * width + x;

        address_space_read(&address_space_memory, src + (hwaddr)row * stride,
                           MEMTXATTRS_UNSPECIFIED, s->rowbuf, w * bpp);
        if (bpp == 4) {
            for (int i = 0; i < w; i++) {
                const uint8_t *p = s->rowbuf + i * 4;
                dst[i] = disp_rgb565(p[2], p[1], p[0]);
            }
        } else {
            for (int i = 0; i < w; i++) {
                dst[i] = s->rowbuf[i * 2] | (s->rowbuf[i * 2 + 1] << 8);
            }
        }
    }
    return true;
}

bool ipod_classic_disp_compose(IPodClassicDispState *s, uint16_t *fb,
                               unsigned width, unsigned height)
{
    bool drew = false;

    /*
     * Slot order is Z order, lowest index at the back. INFERRED: layer 0 is the
     * only one that is ever full-screen and the ones above it are panes that
     * slide over it, so any other order would put the background on top. No
     * measurement distinguishes them, because no two layers have ever
     * overlapped in a boot we have traced.
     *
     * A layer is drawn when it has a source address and a positive size, and
     * nothing else gates it. The control register at +0x008 does carry three
     * fields that look like per-layer enables (bits 16, 20 and 24) but they are
     * all set once during init and never touched again, so using them as the
     * gate would be reading meaning into a constant.
     */
    for (int k = 0; k < S5L8702_DISP_NSLOTS; k++) {
        drew |= disp_draw_layer(s, k, fb, width, height);
    }
    return drew;
}

static const MemoryRegionOps ipod_classic_disp_ops = {
    .read = ipod_classic_disp_read,
    .write = ipod_classic_disp_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static void ipod_classic_disp_reset(DeviceState *dev)
{
    IPodClassicDispState *s = IPOD_CLASSIC_DISP(dev);

    /*
     * All zero. NOT a measurement: we have no reset values for this block, and
     * zero is what the guest saw while it was unimplemented, so nothing that
     * worked before can break on it.
     */
    memset(s->reg, 0, sizeof(s->reg));
}

static void ipod_classic_disp_realize(DeviceState *dev, Error **errp)
{
    IPodClassicDispState *s = IPOD_CLASSIC_DISP(dev);

    memory_region_init_io(&s->iomem, OBJECT(s), &ipod_classic_disp_ops, s,
                          TYPE_IPOD_CLASSIC_DISP, S5L8702_DISP_MEM_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);
}

static const VMStateDescription vmstate_ipod_classic_disp = {
    .name = TYPE_IPOD_CLASSIC_DISP,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(reg, IPodClassicDispState, S5L8702_DISP_NREG),
        VMSTATE_END_OF_LIST()
    },
};

static void ipod_classic_disp_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "S5L8702 display pipe";
    dc->realize = ipod_classic_disp_realize;
    dc->reset = ipod_classic_disp_reset;
    dc->vmsd = &vmstate_ipod_classic_disp;
    dc->user_creatable = false;
}

static const TypeInfo ipod_classic_disp_info = {
    .name          = TYPE_IPOD_CLASSIC_DISP,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IPodClassicDispState),
    .class_init    = ipod_classic_disp_class_init,
};

static void ipod_classic_disp_register_types(void)
{
    type_register_static(&ipod_classic_disp_info);
}

type_init(ipod_classic_disp_register_types)
