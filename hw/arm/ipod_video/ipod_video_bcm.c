/*
 * Broadcom BCM2722 video companion chip, as wired on the iPod 5G.
 *
 * The PP502x does not drive the panel itself. It talks to this chip through a
 * narrow window of registers 64 KB apart at 0x30000000, and the chip owns both
 * the framebuffer and the panel. From Rockbox's lcd-video.c:
 *
 *     bcm_write32(addr, val):  BCM_WR_ADDR32 = addr;
 *                              while (!(BCM_CONTROL & 0x2));
 *                              BCM_DATA32 = val;
 *
 *     bcm_read32(addr):        while (!(BCM_RD_ADDR & 1));
 *                              BCM_RD_ADDR32 = addr;
 *                              while (!(BCM_CONTROL & 0x10));
 *                              return BCM_DATA32;
 *
 * So: a pointer register, a data port that auto-increments, and ready bits.
 *
 * On top of that sits the chip's own processor, the VideoCore. Both guests
 * bootstrap it the same way and with the same constants -- Rockbox in
 * bcm_init() at lcd-video.c:538-598, Apple in bcm_load() at 0x287698, and the
 * two agree instruction for instruction:
 *
 *     eight bootstrap bytes to BCM_CONTROL, five to BCM_ALT_CONTROL
 *     upload the ~200 KB VMCS image to chip address 0
 *     BCMA_COMMAND  = 0
 *     [0x10000C00]  = 0xC0000000        -- release the VideoCore
 *     wait until [0x10000C00] & 1       -- it answers that it is executing
 *     [0x10000C00]  = 0
 *     [0x10000400]  = 0xA5A50002        -- ring its doorbell
 *     wait until BCMA_COMMAND != 0      -- it answers with a completion code
 *
 * We do not execute the uploaded image, so those two answers have to come from
 * here. That is modelling, not invention: what the chip must say is pinned by
 * the code that interrogates it, and where it is pinned exactly is written
 * beside each answer below. The one place a value could not be derived is
 * marked ASSUMED.
 *
 * What is modelled: the pointer/data/ready protocol on both ports, the chip's
 * address space as plain memory, the bootstrap handshake, the VideoCore
 * release and doorbell, and the two update commands, which push the
 * framebuffer to a QEMU console.
 *
 * What is not: TV out, YUV blitting, sleep, and the VideoCore's host interface
 * -- the ring channels the retailOS goes looking for once the handshake
 * succeeds, whose base pointer it reads from BCMA_STATUS at 0x288088. We
 * publish no such pointer, which is a state that driver handles (0x288090
 * treats a null base as "no interface" and gives up cleanly) rather than one
 * it chokes on.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "ui/console.h"
#include "hw/arm/ipod_video/ipod_video_bcm.h"

/* Registers, which sit 64 KB apart. */
#define BCM_REG_DATA        0
#define BCM_REG_WR_ADDR     1
#define BCM_REG_RD_ADDR     2
#define BCM_REG_CONTROL     3
#define BCM_REG_ALT_DATA    4
#define BCM_REG_ALT_WR_ADDR 5
#define BCM_REG_ALT_RD_ADDR 6
#define BCM_REG_ALT_CONTROL 7

/*
 * CONTROL bits the firmware spins on. All four mean "the chip is ready", and
 * they are read as a halfword.
 *
 * The two boot bits are the bootstrap handshake, and Rockbox's bcm_init()
 * names them by how it uses them -- lcd-video.c:559-560, and again at 579-580
 * before the firmware upload:
 *
 *     while (BCM_ALT_CONTROL & 0x80);        -- wait while busy
 *     while (!(BCM_ALT_CONTROL & 0x40));     -- wait until ready
 *
 * Apple's retailOS runs the same two loops, at 0x2876d0 and 0x2876dc. The
 * difference is that Rockbox skips the whole bootstrap when GPO32 bit 14 says
 * the chip is already powered, and the retailOS does not: it brings the chip
 * up itself every time. That is why this only became visible now.
 */
#define BCM_CONTROL_WR_READY   0x02
#define BCM_CONTROL_RD_READY   0x10
#define BCM_CONTROL_BOOT_READY 0x40
#define BCM_CONTROL_BOOT_BUSY  0x80

/* Addresses inside the chip. */
#define BCMA_COMMAND    0x001F8
#define BCMA_STATUS     0x001FC
#define BCMA_CMDPARAM   0xE0000   /* where a frame is written */

/*
 * The VideoCore's release register and its doorbell, in the control window.
 * Neither has a name in any public source; these are what the two drivers do
 * with them. lcd-video.c:587-592 and Apple 0x287850-0x2878ac.
 */
#define BCM_VC_RELEASE  0x0C00    /* within the control window */
#define BCM_VC_DOORBELL 0x0400

/*
 * Writing this register with bit 31 set is what starts the uploaded firmware.
 * The driver writes 0xC0000000, bits 31 and 30; what the two mean separately
 * is UNDETERMINED, so the trigger is the top bit alone. It is an edge, not a
 * level: the driver clears the whole register two writes later
 * (lcd-video.c:591) while the VideoCore is demonstrably still running, and
 * runs the bootstrap byte sequence over the port again at 0x28805c expecting
 * to find it running still. So nothing here stops it once started.
 */
#define BCM_VC_RELEASE_GO   0x80000000
/*
 * And bit 0 read back is the VideoCore's acknowledgement that it is executing.
 * Both drivers test that one bit and nothing else -- "tst r0, #1" over a
 * halfword in Apple's 0x287880, "& 1" in lcd-video.c:589 -- and then write the
 * register to zero and never read it again, so the rest of the word is free.
 */
#define BCM_VC_RELEASE_ACK  0x00000001

/*
 * The doorbell carries 0xA5A5 in its top half. The low half selects something
 * -- boot uses 2 -- but what, is UNDETERMINED: the retailOS rings the same
 * doorbell again from its streaming path at 0x27eff4, so the low half cannot
 * be treated as "this is the boot one".
 */
#define BCM_VC_DOORBELL_MAGIC 0xA5A50000
#define BCM_VC_DOORBELL_MASK  0xFFFF0000

/*
 * What the VideoCore leaves in BCMA_COMMAND when it has finished something.
 *
 * For the boot handshake this is DERIVED and there is exactly one value that
 * works. Three separate checks bracket it:
 *
 *   - bcm_load spins until it is non-zero (Apple 0x2878b4 "movs r4,r0; beq",
 *     Rockbox lcd-video.c:594).
 *   - its caller treats the value as signed and fails on a negative
 *     (0x1affc8 "cmp r6,#0 / bge"), so bit 31 must be clear.
 *   - and the routine that goes on to find the host interface requires it to
 *     be *exactly one*: 0x288078 reads sixteen bytes from chip address 0x1F0,
 *     0x28807c takes the word at +8 -- that is 0x1F8 -- and 0x288080 does
 *     "cmp r0, #1 / bne <give up>".
 *
 * The third check leaves no freedom at all, and it also happens to satisfy the
 * other two and to keep clear of the two values Rockbox reads as "still busy"
 * (BCMCMD_LCD_UPDATE and 0xFFFF, lcd-video.c:205).
 *
 * Reusing the same code as the answer to a finished *command* is ASSUMED. What
 * is derived there is only the behaviour -- the chip overwrites BCMA_COMMAND
 * when a command completes, which is what lcd-video.c:500-506 waits for and
 * what lcd-video.c:205 tests -- plus the bounds that it must differ from the
 * command just posted and from 0xFFFF. Any number meeting those would do;
 * there is no source that names one.
 */
#define BCM_VC_DONE     1

/*
 * The host interface a running VideoCore publishes, and where we put ours.
 *
 * The retailOS finds it by reading the word beside the mailbox: 0x288078 pulls
 * sixteen bytes from chip address 0x1F0, 0x288088 takes the word at 0x1FC as a
 * base, 0x288090 rejects a null one and 0x28809c a misaligned one. What sits
 * at that base is three sixteen-byte blocks -- eight halfword offsets naming
 * each channel's descriptor, then a byte of host-to-VideoCore event bits per
 * channel, then a byte of VideoCore-to-host bits (0x2880bc reads the first,
 * 0xcbe48 and 0xd0558 the other two).
 *
 * We publish that structure with every byte zero, which the driver reads as
 * "the interface is here and has no channels": 0x2871d8 loads a channel's
 * offset and 0x2871e0 skips the channel when it is zero. That is the driver's
 * own spelling of an absent channel, so it is the honest way to say we have
 * none -- we do not run the VideoCore, so there is nothing behind a channel to
 * talk to.
 *
 * Publishing nothing at all is not an option, and that took a wall to find
 * out. A null base makes 0x288058 return an error at 0x2880a4 -- but its only
 * caller, 0xcff6c, ignores the return value, so the mutex that the success
 * path would have created at 0x2880dc never exists, and 0xcff8c tail-calls
 * 0x28827c to unlock it anyway. That reads the null pointer, gets the first
 * word of the retailOS's own image back (address 0 is DRAM after the remap,
 * so it does not even fault there), and dies dereferencing 0xEA00007A at
 * 0xa0bd0.
 *
 * The address is ours, and ASSUMED: the real one is wherever the VMCS firmware
 * decides to put it, and nothing outside that firmware knows. Nothing depends
 * on the number -- the driver takes whatever it is told -- so this only has to
 * be inside the chip's memory, four-aligned, and clear of both the firmware
 * image the host uploads (0x312A0 bytes from address 0) and the framebuffer
 * Rockbox writes at 0xE0000.
 */
#define BCM_IFACE_BASE  0x00180000
#define BCM_IFACE_SIZE  0x30

/*
 * Commands are sent as a value and its complement in the two halves of a word,
 * which is what BCM_CMD(x) in lcd-video.c builds.
 */
#define BCM_CMD(x)              ((~((uint32_t)(x)) << 16) | ((uint32_t)(x)))
#define BCMCMD_LCD_UPDATE       BCM_CMD(0)
#define BCMCMD_LCD_UPDATERECT   BCM_CMD(5)

/*
 * Chip memory. Two windows: the low one from address 0, and the VideoCore's
 * control registers at 0x10000000. Everything else -- the chip's SDRAM
 * aperture at 0xC0000000, say -- is reported rather than quietly absorbed.
 */
static uint8_t *bcm_mem_ptr(IPodVideoBCMState *s, uint32_t addr, unsigned size)
{
    if (addr <= BCM_MEM_SIZE - size) {
        return s->mem + addr;
    }
    if (addr >= BCM_CTRL_BASE && addr - BCM_CTRL_BASE <= BCM_CTRL_SIZE - size) {
        return s->ctrl + (addr - BCM_CTRL_BASE);
    }
    return NULL;
}

/*
 * The data port is as wide as the access. Rockbox always uses BCM_DATA32 and
 * writes whole words, but the retailOS writes halfwords through it -- 0x287c94
 * pushes the last 1..255 bytes of a block two at a time, and even a four-byte
 * bcm_write goes out as two halfword stores -- and its DMA engine writes
 * single bytes, because the descriptor it programs for the firmware upload
 * leaves the access-width field at zero, which pp5020.h:699 spells 8-bit. An
 * access that stored more than it was given would trample what follows it.
 */
static void bcm_mem_write(IPodVideoBCMState *s, uint32_t addr, uint32_t value,
                          unsigned size)
{
    uint8_t *p = bcm_mem_ptr(s, addr, size);

    if (!p) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: write to unmodelled chip address 0x%x\n",
                      __func__, addr);
        return;
    }
    switch (size) {
    case 1:
        *p = value;
        break;
    case 2:
        stw_le_p(p, value);
        break;
    default:
        stl_le_p(p, value);
        break;
    }
}

static uint32_t bcm_mem_read(IPodVideoBCMState *s, uint32_t addr, unsigned size)
{
    uint8_t *p = bcm_mem_ptr(s, addr, size);
    uint32_t value;

    if (!p) {
        qemu_log_mask(LOG_UNIMP,
                      "%s: read from unmodelled chip address 0x%x\n",
                      __func__, addr);
        return 0;
    }
    switch (size) {
    case 1:
        value = *p;
        break;
    case 2:
        value = lduw_le_p(p);
        break;
    default:
        value = ldl_le_p(p);
        break;
    }

    /*
     * Bit 0 of the release register reads set while the VideoCore is running.
     * It is folded in here rather than stored, because the driver clears the
     * register straight after seeing it and the bit is a property of the chip,
     * not of what was last written.
     */
    if (s->vc_running && addr == BCM_CTRL_BASE + BCM_VC_RELEASE) {
        value |= BCM_VC_RELEASE_ACK;
    }
    return value;
}

/* The whole 32-bit word an access fell inside, after that access. */
static uint32_t bcm_mem_word(IPodVideoBCMState *s, uint32_t addr)
{
    uint8_t *p = bcm_mem_ptr(s, addr & ~3u, 4);

    return p ? ldl_le_p(p) : 0;
}

/*
 * The address registers are 16 bits wide, and a 32-bit address goes in as two
 * halfwords, low first -- 0x287c28 and 0x287c2c in Apple's firmware:
 *
 *     strh r9, [r8]          ; BCM_WR_ADDR = addr
 *     lsr  r0, r9, #16
 *     strh r0, [r8]          ; BCM_WR_ADDR = addr >> 16
 *
 * so the register behaves as a shift register loaded from the top. Rockbox
 * only ever writes the whole word at once through BCM_WR_ADDR32, which is why
 * simply assigning the value worked until now and would have silently uploaded
 * the video firmware to the wrong place.
 */
static uint32_t bcm_addr_shift(uint32_t reg, uint32_t value, unsigned size)
{
    return size == 2 ? (reg >> 16) | (value << 16) : value;
}

static void bcm_run_command(IPodVideoBCMState *s, uint32_t cmd)
{
    switch (cmd) {
    case BCMCMD_LCD_UPDATE:
    case BCMCMD_LCD_UPDATERECT:
        s->dirty = true;
        break;
    case 0:
        /* Clearing the mailbox is not a command; bcm_load does it twice. */
        return;
    default:
        qemu_log_mask(LOG_UNIMP, "%s: command 0x%08x is not modelled\n",
                      __func__, cmd);
        return;
    }
    /*
     * Report the command as finished by overwriting the mailbox, which is what
     * the real chip does and what both drivers watch for: Rockbox's lcd_tick
     * calls anything other than the posted command or 0xFFFF "not busy"
     * (lcd-video.c:204-206) and Apple's bcm_command loops until the same
     * (lcd-video.c:500-506 is Rockbox's copy of it).
     *
     * This used to write zero to BCMA_STATUS instead, a register no guest ever
     * reads -- and worse, the one the retailOS reads the host interface base
     * pointer out of at 0x288088.
     */
    stl_le_p(s->mem + BCMA_COMMAND, BCM_VC_DONE);
}

/*
 * Chip-side side effects of a write, once the whole word is in place. Both
 * control words are written as two halfwords by the retailOS, so acting on the
 * value of a single access would see 0x0000 where 0xC0000000 was meant.
 */
static void bcm_wrote(IPodVideoBCMState *s, IPodVideoBCMPort *p, uint32_t addr)
{
    uint32_t word;

    if (addr >= BCM_CTRL_BASE) {
        uint32_t off = addr - BCM_CTRL_BASE;

        if ((off & ~3u) == BCM_VC_RELEASE) {
            word = bcm_mem_word(s, addr);
            if ((word & BCM_VC_RELEASE_GO) && !s->vc_running) {
                s->vc_running = true;
            }
        } else if ((off & ~3u) == BCM_VC_DOORBELL) {
            word = bcm_mem_word(s, addr);
            if ((word & BCM_VC_DOORBELL_MASK) == BCM_VC_DOORBELL_MAGIC
                && s->vc_running) {
                /*
                 * The doorbell is what the firmware waits on: it writes zero to
                 * the mailbox, releases the core, and only after ringing this
                 * does it spin on the mailbox going non-zero. So this is where
                 * the answer belongs, not on the release.
                 */
                stl_le_p(s->mem + BCMA_COMMAND, BCM_VC_DONE);
                /*
                 * And the word beside it has to be answered too: it is where
                 * the VideoCore publishes its host interface. Writing it is
                 * not optional -- chip address 0x1FC sits inside the region
                 * the VMCS upload has just written, so leaving it alone hands
                 * the retailOS whatever byte of the image landed there and it
                 * follows it as a pointer.
                 */
                memset(s->mem + BCM_IFACE_BASE, 0, BCM_IFACE_SIZE);
                stl_le_p(s->mem + BCMA_STATUS, BCM_IFACE_BASE);
            }
        }
        return;
    }

    /*
     * A command is posted by pointing the write port at the mailbox and
     * writing it. A bulk transfer whose pointer happens to sweep across the
     * mailbox on its way past is not a post -- and on the real chip could not
     * be, since what sweeps across it is the VideoCore's own firmware image
     * being loaded while the VideoCore is halted. Hence the check on where the
     * run was aimed rather than on where this write landed.
     */
    if ((addr & ~3u) == BCMA_COMMAND && p->wr_start == BCMA_COMMAND) {
        bcm_run_command(s, bcm_mem_word(s, addr));
    }
}

static uint64_t ipod_video_bcm_read(void *opaque, hwaddr offset, unsigned size)
{
    IPodVideoBCMState *s = opaque;
    IPodVideoBCMPort *p = &s->port[(offset >> 16) >= BCM_REG_ALT_DATA ? 1 : 0];
    uint32_t value;

    switch (offset >> 16) {
    case BCM_REG_DATA:
    case BCM_REG_ALT_DATA:
        value = bcm_mem_read(s, p->rd_addr, size);
        p->rd_addr += size;
        return value;

    case BCM_REG_RD_ADDR:
    case BCM_REG_ALT_RD_ADDR:
        /* Bit 0 says the chip will accept a new read address. It always will. */
        return 1;

    case BCM_REG_CONTROL:
    case BCM_REG_ALT_CONTROL:
        /*
         * Never busy, always ready: there is no bus behind this to be slow and
         * no bootstrap for it to be part-way through.
         */
        return BCM_CONTROL_WR_READY | BCM_CONTROL_RD_READY
             | BCM_CONTROL_BOOT_READY;

    case BCM_REG_WR_ADDR:
    case BCM_REG_ALT_WR_ADDR:
        return p->wr_addr;

    default:
        qemu_log_mask(LOG_UNIMP, "%s: unknown register at +0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }
}

static void ipod_video_bcm_write(void *opaque, hwaddr offset, uint64_t value,
                                 unsigned size)
{
    IPodVideoBCMState *s = opaque;
    IPodVideoBCMPort *p = &s->port[(offset >> 16) >= BCM_REG_ALT_DATA ? 1 : 0];
    uint32_t addr;

    switch (offset >> 16) {
    case BCM_REG_DATA:
    case BCM_REG_ALT_DATA:
        addr = p->wr_addr;
        bcm_mem_write(s, addr, value, size);
        p->wr_addr += size;
        bcm_wrote(s, p, addr);
        break;

    case BCM_REG_WR_ADDR:
    case BCM_REG_ALT_WR_ADDR:
        p->wr_addr = bcm_addr_shift(p->wr_addr, value, size);
        p->wr_start = p->wr_addr;
        break;

    case BCM_REG_RD_ADDR:
    case BCM_REG_ALT_RD_ADDR:
        p->rd_addr = bcm_addr_shift(p->rd_addr, value, size);
        break;

    case BCM_REG_CONTROL:
    case BCM_REG_ALT_CONTROL:
        /*
         * The bootstrap byte sequence and the 0x31 kick both land here. The
         * kick is the host telling the chip to look at what it just posted,
         * and since a post is acted on as it happens there is nothing left for
         * it to do. The bootstrap does not reset the VideoCore either: 0x28805c
         * runs the same sequence again and then expects to find the firmware
         * still executing.
         */
        break;

    default:
        qemu_log_mask(LOG_UNIMP, "%s: unknown register at +0x%" HWADDR_PRIx
                      " (wrote 0x%" PRIx64 ")\n", __func__, offset, value);
        break;
    }
}

static const MemoryRegionOps ipod_video_bcm_ops = {
    .read = ipod_video_bcm_read,
    .write = ipod_video_bcm_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static void ipod_video_bcm_gfx_update(void *opaque)
{
    IPodVideoBCMState *s = opaque;

    if (!s->dirty) {
        return;
    }
    s->dirty = false;
    dpy_gfx_update_full(s->con);
}

static void ipod_video_bcm_invalidate(void *opaque)
{
    IPodVideoBCMState *s = opaque;

    s->dirty = true;
}

static const GraphicHwOps ipod_video_bcm_gfx_ops = {
    .invalidate = ipod_video_bcm_invalidate,
    .gfx_update = ipod_video_bcm_gfx_update,
};

static void ipod_video_bcm_reset(DeviceState *dev)
{
    IPodVideoBCMState *s = IPOD_VIDEO_BCM(dev);

    memset(s->port, 0, sizeof(s->port));
    s->dirty = true;
    /*
     * The chip comes up with its firmware running. That is the same fiction
     * this machine already tells through GPO32_VAL bit 14, which reports the
     * Broadcom as powered so that Rockbox skips the bootstrap it cannot
     * complete without a VMCS image from the NOR flash we do not have. The
     * retailOS ignores that bit and bootstraps the chip itself, which takes
     * this back through false and out the other side.
     */
    s->vc_running = true;
    memset(s->mem, 0, BCM_MEM_SIZE);
    memset(s->ctrl, 0, BCM_CTRL_SIZE);
}

static void ipod_video_bcm_realize(DeviceState *dev, Error **errp)
{
    IPodVideoBCMState *s = IPOD_VIDEO_BCM(dev);
    DisplaySurface *surface;

    s->con = graphic_console_init(dev, 0, &ipod_video_bcm_gfx_ops, s);
    qemu_console_resize(s->con, LCD_WIDTH, LCD_HEIGHT);

    /*
     * Point the console straight at the chip's memory where the firmware
     * writes pixels, so an update is just a repaint rather than a copy. The
     * panel is 320x240 RGB565, per Rockbox's config for this target.
     *
     * This is Rockbox's framebuffer address. The retailOS does not use it: it
     * never loads 0xE0000 anywhere in seven megabytes, and lets the VideoCore
     * pull pixels out of host memory at addresses that only exist in messages
     * the VideoCore sends. So this surface stays black under Apple's firmware
     * however well the handshake goes.
     */
    surface = qemu_create_displaysurface_from(LCD_WIDTH, LCD_HEIGHT,
                                              PIXMAN_r5g6b5,
                                              LCD_WIDTH * 2,
                                              s->mem + BCMA_CMDPARAM);
    dpy_gfx_replace_surface(s->con, surface);
}

static void ipod_video_bcm_init(Object *obj)
{
    IPodVideoBCMState *s = IPOD_VIDEO_BCM(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    s->mem = g_malloc0(BCM_MEM_SIZE);
    s->ctrl = g_malloc0(BCM_CTRL_SIZE);

    memory_region_init_io(&s->iomem, obj, &ipod_video_bcm_ops, s,
                          TYPE_IPOD_VIDEO_BCM, BCM_MMIO_SIZE);
    sysbus_init_mmio(sbd, &s->iomem);
    sysbus_init_irq(sbd, &s->irq);
}

static const VMStateDescription vmstate_ipod_video_bcm_port = {
    .name = "ipod_video_bcm_port",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(wr_addr, IPodVideoBCMPort),
        VMSTATE_UINT32(rd_addr, IPodVideoBCMPort),
        VMSTATE_UINT32(wr_start, IPodVideoBCMPort),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_ipod_video_bcm = {
    .name = TYPE_IPOD_VIDEO_BCM,
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(port, IPodVideoBCMState, BCM_NUM_PORTS, 1,
                             vmstate_ipod_video_bcm_port, IPodVideoBCMPort),
        VMSTATE_BOOL(vc_running, IPodVideoBCMState),
        VMSTATE_END_OF_LIST()
    }
};

static void ipod_video_bcm_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = ipod_video_bcm_realize;
    dc->reset = ipod_video_bcm_reset;
    dc->vmsd = &vmstate_ipod_video_bcm;
    dc->desc = "Broadcom BCM2722 video companion";
}

static const TypeInfo ipod_video_bcm_info = {
    .name          = TYPE_IPOD_VIDEO_BCM,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IPodVideoBCMState),
    .instance_init = ipod_video_bcm_init,
    .class_init    = ipod_video_bcm_class_init,
};

static void ipod_video_bcm_register_types(void)
{
    type_register_static(&ipod_video_bcm_info);
}

type_init(ipod_video_bcm_register_types)
