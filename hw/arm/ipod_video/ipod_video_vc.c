/*
 * The VideoCore's host interface, modelled rather than executed.
 *
 * WHY THIS EXISTS
 * ---------------
 * On the iPod 5G the PP502x does not paint Apple's user interface. It hands
 * whole surfaces to the BCM2722 and the chip's own processor, the VideoCore,
 * composites and scans them out. We upload the real firmware image (vmcs.bin,
 * pulled off the device's own firmware partition) into the chip's memory, but
 * we do not run it: it is Broadcom VideoCore code, an ISA QEMU has no backend
 * for. Everything below is the *protocol* that firmware would speak, derived
 * from the driver that talks to it.
 *
 * The whole thing sits in chip memory, which for us is just IPodVideoBCMState's
 * mem[] buffer. The guest reaches it through the two host ports, so every
 * message arrives as a run of writes through ipod_video_bcm.c and every reply
 * is bytes we leave behind for it to read back.
 *
 * WHAT IS DERIVED AND FROM WHERE
 * ------------------------------
 * Addresses are file offsets into assets/images-5g/apple_retailos.bin, which is
 * also its load address. Nothing here is a value picked to make a check pass;
 * where a number could not be derived it is marked ASSUMED, and the ones that
 * are ours to choose (where in chip memory things live) say so.
 *
 *   0x288058  finds the interface: sixteen bytes from chip address 0x1F0, the
 *             word at 0x1F8 must be exactly 1, the word at 0x1FC is the base.
 *   0x2880b0  copies sixteen bytes from that base: eight u16 channel offsets.
 *   0x2871d4  indexes them; 0x2871e0 skips a channel whose offset is zero.
 *   0x2871f8  reads sixteen bytes from base+offset and 0x287200 wants the u16
 *             at +4 to be 1. 0x286b18 wants 2, 0xd52e8 5, 0xd54b0 6,
 *             0x288a04 7. That u16 is the channel type.
 *   0x2882c0  reads eighty bytes of channel control block, and the four
 *             consumers below say what is in them.
 *   0x28861c  composes a message: sixteen-byte header then the payload padded
 *             to a multiple of sixteen.
 *   0x2886a0  the header magic, 0xF1A55A1F. 0x287360 refuses a reply without it.
 *   0x28871c  copies into the host->VC ring; 0x288434 copies out of the VC->host
 *             ring; 0xf5834 is the shared occupancy helper.
 *   0x288800  publishes the write pointer and rings the doorbell.
 *   0x287ba4  the doorbell itself: halfword 0x31 to BCM_CONTROL.
 *
 * WHY NO INTERRUPT IS NEEDED
 * --------------------------
 * The reply is deposited before the guest ever looks for it. Every request the
 * retailOS makes is synchronous inside one task -- 0x28861c to send, then
 * 0x2883d4 to read -- and 0x2883d4 only sleeps when the ring is empty
 * (0x2883f8 returns early once it has all it asked for, and 0x288418 refreshes
 * the write pointer out of chip memory before 0x288424 decides to wait). Since
 * we act on the guest's own store of the write pointer, the answer is already
 * in the ring by the time it gets there and 0x2870dc is never reached.
 *
 * That is also why the unsolicited direction is *not* modelled. The dispatcher
 * that would wake a sleeping caller, 0x288100, only runs from the VCIH task at
 * 0x1239b8, which waits on an event at [0x1081eca8] that nothing in seven
 * megabytes writes -- it must come from the BCM's interrupt line, which is
 * UNDETERMINED and not connected. So the VideoCore here never speaks first.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/bswap.h"
#include "qemu/cutils.h"
#include "hw/arm/ipod_video/ipod_video_bcm.h"

/* Where we put the interface. See BCM_IFACE_BASE in ipod_video_bcm.c. */
#define VC_IFACE_BASE       0x00180000

/*
 * The header, three sixteen-byte blocks, from the routines that read them:
 *   +0x00..0x0f  eight u16 channel offsets      (0x2880b0 / 0x2871d4)
 *   +0x10        host's signal byte             (0x28823c writes it)
 *   +0x11        host's acknowledgement byte    (0x288138 writes it)
 *   +0x20        our acknowledgement byte       (0x288240 reads it)
 *   +0x21        our signal byte                (0x288118 reads it)
 * Signalling is by toggle, not by level: channel n has an event pending when
 * bit n of (signal ^ acknowledgement) is set (0x288244, 0x288120).
 */
#define VC_OFF_HOST_SIG     0x10
#define VC_OFF_HOST_ACK     0x11
#define VC_OFF_VC_ACK       0x20
#define VC_OFF_VC_SIG       0x21

#define VC_NUM_CHANNELS     8

/*
 * Where the control blocks and rings go inside the interface. All of this is
 * ours to choose -- the guest is told each channel's offset and reads the
 * layout out of the block -- so the only constraints are the ones the driver
 * imposes: offsets are u16 relative to the base (0x2871e4 adds them), a
 * control block is eighty bytes (0x2882ec), and the usable ring capacity is
 * its length minus sixteen (0x28830c, 0x288324, 0x28874c).
 *
 * Sixteen bytes is the unit of everything: every pointer lives on its own
 * sixteen-byte line and is transferred by a sixteen-byte operation of its own
 * (0x288928, 0x288800, 0x28850c, 0x2885cc), which is what lets the two sides
 * share a ring with no lock between them.
 */
#define VC_CTRL_OFF(ch)     (0x100 + (ch) * 0x80)
#define VC_CTRL_LEN         0x50

#define VC_RING_LEN         0x800
#define VC_TX_OFF(ch)       (0x1000 + (ch) * 0x1000)
#define VC_RX_OFF(ch)       (VC_TX_OFF(ch) + VC_RING_LEN)

/* Fields of the eighty-byte control block. */
#define VC_CH_TYPE          0x04    /* u16 */
#define VC_CH_TX_START      0x06    /* u16 */
#define VC_CH_TX_END        0x08    /* u16 */
#define VC_CH_RX_START      0x0a    /* u16 */
#define VC_CH_RX_END        0x0c    /* u16 */
#define VC_CH_TX_RD         0x10    /* u16, ours to move */
#define VC_CH_TX_WR         0x20    /* u16, the host's */
#define VC_CH_RX_RD         0x30    /* u16, the host's */
#define VC_CH_RX_WR         0x40    /* u16, ours to move */

/*
 * The channels we publish, and which slot each gets. The driver scans all
 * eight looking for a type, so the numbering is free; what matters is that a
 * slot it does not know about stays zero, which is its own spelling of "no
 * channel here" (0x2871e0).
 *
 * Only the two the display needs are published. Types 5 (VCFS), 6 (VCHR) and 7
 * exist in the driver but are directions we do not speak: 5 and 6 are the
 * VideoCore asking the host for things, which requires the unsolicited path
 * that has no interrupt behind it, and 7 is UNDETERMINED. Leaving them absent
 * is honest and costs nothing -- 0xcff6c ignores every return value.
 */
#define VC_CH_GENCMD        0   /* type 1: ASCII commands */
#define VC_CH_GFX           1   /* type 2: binary display RPC */

#define VC_TYPE_GENCMD      1
#define VC_TYPE_GFX         2

/* The message header, 0x28861c composing and 0x287354/0x2872fc checking. */
#define VC_MSG_MAGIC        0xF1A55A1FU
#define VC_MSG_HDR_LEN      16
#define VC_MSG_OFF_MAGIC    0
#define VC_MSG_OFF_SEQ      4
#define VC_MSG_OFF_OPCODE   8
#define VC_MSG_OFF_LENGTH   12  /* u16 */
#define VC_MSG_OFF_FLAGS    14  /* u16, always zero, meaning UNDETERMINED */

/* Longest thing either side sends: 0xe9394 formats into a 256-byte buffer. */
#define VC_MAX_PAYLOAD      512

/*
 * Where surfaces live in chip memory. Ours to choose, and only has to be clear
 * of the firmware image the host uploads (0 to 0x312A0), the framebuffer
 * Rockbox writes at 0xE0000, and this interface.
 */
#define VC_SURFACE_HEAP     0x00200000

static inline uint8_t *vc_iface(IPodVideoBCMState *s, uint32_t off)
{
    return s->mem + VC_IFACE_BASE + off;
}

static inline uint16_t vc_ld16(IPodVideoBCMState *s, uint32_t off)
{
    return lduw_le_p(vc_iface(s, off));
}

static inline void vc_st16(IPodVideoBCMState *s, uint32_t off, uint16_t v)
{
    stw_le_p(vc_iface(s, off), v);
}

static inline void vc_st32(IPodVideoBCMState *s, uint32_t off, uint32_t v)
{
    stl_le_p(vc_iface(s, off), v);
}

/* 0xf5834, byte for byte: how much of a ring is occupied, wrap included. */
static uint32_t vc_occupancy(uint16_t start, uint16_t end,
                             uint16_t rd, uint16_t wr)
{
    if (wr < rd) {
        return (end - rd) + (wr - start);
    }
    return wr - rd;
}

/* Copy len bytes out of a ring; a null destination just skips them. */
static void vc_ring_read(IPodVideoBCMState *s, uint16_t start, uint16_t end,
                         uint16_t *rd, uint8_t *dst, uint32_t len)
{
    uint32_t i;

    for (i = 0; i < len; i++) {
        if (dst) {
            dst[i] = *vc_iface(s, *rd);
        }
        if (++*rd == end) {
            *rd = start;
        }
    }
}

static void vc_ring_write(IPodVideoBCMState *s, uint16_t start, uint16_t end,
                          uint16_t *wr, const uint8_t *src, uint32_t len)
{
    uint32_t i;

    for (i = 0; i < len; i++) {
        *vc_iface(s, *wr) = src[i];
        if (++*wr == end) {
            *wr = start;
        }
    }
}

/*
 * Answer to a channel-type-1 command.
 *
 * The vocabulary is the VideoCore firmware's own: assets/images-5g/vmcs.bin
 * registers twenty text commands with gencmd_register, their names in one
 * contiguous block at file offset 0xBE24, and answers in ASCII key=value pairs
 * whose templates sit at 0xBFF8 ("value=%d") and 0xC004 onwards
 * ("error=%d error_msg=..."). The retailOS's half of that is 0xe9358, which
 * sends the command NUL included with opcode 1, and 0x2872fc, which reads the
 * reply back as NUL-terminated text.
 *
 * What the reply must satisfy is narrow and verified:
 *   - 0x287360 rejects a header without the magic;
 *   - 0x2872c0 returns success only if at least one payload byte came back
 *     (0x2872f0 "rsbs r0, r4, #1"), so an empty *payload* is not allowed even
 *     though an empty *string* is;
 *   - 0x2874a0 looks for the NUL with memchr, so it must be terminated.
 *
 * What the text should say is not derivable for most commands: the only caller
 * that inspects it is 0x287250, and the display path does not
 * (0x164eec tests the return of 0x287664 and nothing else). So a command the
 * firmware registers gets an empty success string -- the honest answer for a
 * setter, and the one thing here that cannot be a wrong number -- and anything
 * else gets the firmware's own "not registered" error template verbatim.
 *
 * ASSUMED: that every registered command succeeds. We are not the VideoCore and
 * cannot fail for its reasons.
 */
static const char *const vc_gencmd_names[] = {
    /* vmcs.bin 0xBE24..0xBF4F, in file order. */
    "tasks", "version", "commands", "power_down", "set_vll_dir",
    "led_control", "disk_notify", "usb_inserted", "audio_enable",
    "inuse_notify", "power_control", "audio_control", "motor_control",
    "camera_control", "display_control", "end_application",
    "power_management", "load_application", "vmcs_display_clear",
    "vmcs_display_enable",
    /* vmcs.bin 0xF98C..0xF9CC, the power_management subcommands, which the
     * retailOS also sends as bare commands (0x1b01e4, 0x1b0200). */
    "pm_set_policy", "pm_get_status", "pm_show_stats",
    "pm_start_logging", "pm_stop_logging",
};

static bool vc_gencmd_known(const char *cmd)
{
    size_t i, n;

    n = strcspn(cmd, " \t");
    for (i = 0; i < ARRAY_SIZE(vc_gencmd_names); i++) {
        if (strlen(vc_gencmd_names[i]) == n
            && !strncmp(cmd, vc_gencmd_names[i], n)) {
            return true;
        }
    }
    return false;
}

static uint32_t vc_handle_gencmd(IPodVideoBCMState *s, const char *cmd,
                                 char *reply, uint32_t reply_max)
{
    bool known = vc_gencmd_known(cmd);

    if (known) {
        reply[0] = '\0';
    } else {
        /* vmcs.bin 0x95F4, verbatim. */
        pstrcpy(reply, reply_max, "error=1 error_msg=\"Command not registered\"");
    }
    qemu_log_mask(LOG_UNIMP, "ipod_video_vc: gencmd \"%s\" -> \"%s\"\n",
                  cmd, reply);
    return strlen(reply) + 1;
}

/*
 * Channel type 2, the binary display RPC. Every call is the same shape: send a
 * 16- or 32-byte payload with the opcode in the header, read 32 bytes back and
 * take the first word of the reply payload (0x286b6c, 0x286c24, 0x286ca8,
 * 0x286d74, 0x286df8, 0x286eb4, 0x286f34 all do exactly that). The magic is
 * not checked on this path; 0x2883d4 hands the bytes over unexamined.
 *
 * Opcode 8 is the one that matters, and it is what makes pixels reachable:
 *
 *   0x286ca8 builds the payload out of the surface descriptor 0x286a1c filled
 *   in -- +4 format byte, +8 width, +12 height, +16 stride, +24 the chip
 *   address the caller wants -- and 0x286d40/0x286d50 take reply word 0 as a
 *   handle and reply word 1 as the address, storing the address into the
 *   descriptor only when the caller asked for zero. Zero means "you choose",
 *   so we do.
 *
 *   Then 0x164500 checks that address is non-zero and 0x164524 pushes the whole
 *   host bitmap into it through the alternate port, and afterwards 0x165024
 *   pushes only the dirty band. So after this reply the guest writes Apple's
 *   framebuffer straight into our chip memory, unprompted.
 *
 * Opcode 3 is the present: 0x286b6c puts the handle at payload+12 and the
 * width and height at payload+24 and +26 (u16 each, from 0x165074 stacking
 * descriptor +4 and +8). That is the moment to repaint.
 *
 * The rest -- 1, 2, 4, 9, 16 -- pass tokens between themselves and are never
 * branched on by the host; what they mean is UNDETERMINED, and answering zero
 * is ASSUMED.
 */
#define VC_GFX_PRESENT      3
#define VC_GFX_CREATE       8

static IPodVideoVCSurface *vc_surface_for(IPodVideoBCMState *s, uint32_t handle)
{
    if (handle == 0 || handle > VC_MAX_SURFACES) {
        return NULL;
    }
    return &s->surface[handle - 1];
}

static uint32_t vc_handle_gfx(IPodVideoBCMState *s, uint32_t opcode,
                              const uint8_t *req, uint32_t req_len,
                              uint8_t *reply)
{
    memset(reply, 0, 16);

    switch (opcode) {
    case VC_GFX_CREATE: {
        uint32_t want, size, i, handle = 0;
        IPodVideoVCSurface *surf = NULL;
        uint8_t format;
        uint32_t width, height, stride;

        if (req_len < 32) {
            break;
        }
        format = req[4];
        width  = ldl_le_p(req + 8);
        height = ldl_le_p(req + 12);
        stride = ldl_le_p(req + 16);
        want   = ldl_le_p(req + 24);
        size   = (uint64_t)stride * height > BCM_MEM_SIZE ? 0 : stride * height;

        for (i = 0; i < VC_MAX_SURFACES; i++) {
            if (!s->surface[i].valid) {
                surf = &s->surface[i];
                handle = i + 1;         /* one-based, so a handle is never 0 */
                break;
            }
        }
        if (!surf || size == 0) {
            qemu_log_mask(LOG_UNIMP, "ipod_video_vc: no room for a %ux%u "
                          "surface of stride %u\n", width, height, stride);
            break;
        }

        if (want == 0) {
            want = s->vc_heap;
            s->vc_heap = (s->vc_heap + size + 15) & ~15u;
        }
        if ((uint64_t)want + size > BCM_MEM_SIZE) {
            qemu_log_mask(LOG_UNIMP, "ipod_video_vc: surface at 0x%x does not "
                          "fit in chip memory\n", want);
            break;
        }

        surf->valid  = true;
        surf->addr   = want;
        surf->width  = width;
        surf->height = height;
        surf->stride = stride;
        surf->size   = size;
        surf->format = format;

        stl_le_p(reply, handle);
        stl_le_p(reply + 4, surf->addr);
        qemu_log_mask(LOG_UNIMP, "ipod_video_vc: surface %u = %ux%u fmt %u "
                      "stride %u at chip 0x%x\n", handle, width, height,
                      format, stride, surf->addr);
        break;
    }

    case VC_GFX_PRESENT: {
        uint32_t handle;
        IPodVideoVCSurface *surf;

        if (req_len < 32) {
            break;
        }
        handle = ldl_le_p(req + 12);
        surf = vc_surface_for(s, handle);
        if (!surf || !surf->valid) {
            qemu_log_mask(LOG_UNIMP, "ipod_video_vc: present of unknown "
                          "surface %u\n", handle);
            break;
        }
        ipod_video_bcm_scanout(s, surf->addr, surf->width, surf->height,
                               surf->stride, surf->format);
        break;
    }

    default:
        break;
    }
    return 16;
}

/*
 * One message off a channel's host->VC ring, answered on the VC->host ring.
 */
static void vc_dispatch(IPodVideoBCMState *s, int ch, uint32_t seq,
                        uint32_t opcode, const uint8_t *payload, uint32_t len)
{
    uint8_t reply[VC_MSG_HDR_LEN + VC_MAX_PAYLOAD];
    char text[VC_MAX_PAYLOAD];
    uint32_t reply_len = 0;
    uint32_t padded, space;
    uint16_t start, end, rd, wr;
    uint32_t ctrl = VC_CTRL_OFF(ch);

    switch (ch) {
    case VC_CH_GENCMD:
        if (opcode != 1) {
            qemu_log_mask(LOG_UNIMP, "ipod_video_vc: gencmd opcode %u is not "
                          "modelled\n", opcode);
            return;
        }
        /* 0xe93ac sends the length with the NUL, but do not trust it. */
        memcpy(text, payload, MIN(len, sizeof(text) - 1));
        text[MIN(len, sizeof(text) - 1)] = '\0';
        reply_len = vc_handle_gencmd(s, text, (char *)reply + VC_MSG_HDR_LEN,
                                     VC_MAX_PAYLOAD);
        break;

    case VC_CH_GFX:
        reply_len = vc_handle_gfx(s, opcode, payload, len,
                                  reply + VC_MSG_HDR_LEN);
        break;

    default:
        return;
    }

    /*
     * The reply reuses the request's header: the VideoCore's own responder
     * overwrites only the result and the length over the buffer the request
     * came in (0xfa0a8, 0xfa0b8), so magic and sequence come back identical.
     * The word at +8 carries a result code rather than the opcode; zero is
     * success, and none of the callers on either channel branch on it.
     */
    stl_le_p(reply + VC_MSG_OFF_MAGIC, VC_MSG_MAGIC);
    stl_le_p(reply + VC_MSG_OFF_SEQ, seq);
    stl_le_p(reply + VC_MSG_OFF_OPCODE, 0);
    stw_le_p(reply + VC_MSG_OFF_LENGTH, reply_len);
    stw_le_p(reply + VC_MSG_OFF_FLAGS, 0);

    padded = VC_MSG_HDR_LEN + ((reply_len + 15) & ~15u);

    start = vc_ld16(s, ctrl + VC_CH_RX_START);
    end   = vc_ld16(s, ctrl + VC_CH_RX_END);
    rd    = vc_ld16(s, ctrl + VC_CH_RX_RD);
    wr    = vc_ld16(s, ctrl + VC_CH_RX_WR);

    /* The sixteen bytes the ring never fills are what keep wr off rd. */
    space = (end - start - 16) - vc_occupancy(start, end, rd, wr);
    if (padded > space) {
        qemu_log_mask(LOG_UNIMP, "ipod_video_vc: channel %d reply of %u bytes "
                      "does not fit in %u\n", ch, padded, space);
        return;
    }

    vc_ring_write(s, start, end, &wr, reply, padded);
    vc_st16(s, ctrl + VC_CH_RX_WR, wr);

    /*
     * And toggle our bit so that signal ^ acknowledgement has it set, which is
     * how 0x288120 spells "channel ch has something". Nothing reads it today,
     * because the task that would (0x1239b8 through 0x288100) is never woken;
     * it is here because it is the other half of what 0x288138 does and
     * leaving it out would be a lie in chip memory.
     */
    *vc_iface(s, VC_OFF_VC_SIG) ^= 1u << ch;
}

/* Drain everything complete the host has posted on a channel. */
static void vc_pump(IPodVideoBCMState *s, int ch)
{
    uint32_t ctrl = VC_CTRL_OFF(ch);
    uint16_t start, end, rd, wr;

    if (!s->vc_iface_live || vc_ld16(s, ctrl + VC_CH_TYPE) == 0) {
        return;
    }

    start = vc_ld16(s, ctrl + VC_CH_TX_START);
    end   = vc_ld16(s, ctrl + VC_CH_TX_END);
    rd    = vc_ld16(s, ctrl + VC_CH_TX_RD);
    wr    = vc_ld16(s, ctrl + VC_CH_TX_WR);

    for (;;) {
        uint8_t hdr[VC_MSG_HDR_LEN];
        uint8_t payload[VC_MAX_PAYLOAD];
        uint32_t avail = vc_occupancy(start, end, rd, wr);
        uint32_t seq, opcode, len, padded;
        uint16_t peek;

        if (avail < VC_MSG_HDR_LEN) {
            break;
        }
        peek = rd;
        vc_ring_read(s, start, end, &peek, hdr, VC_MSG_HDR_LEN);

        if (ldl_le_p(hdr + VC_MSG_OFF_MAGIC) != VC_MSG_MAGIC) {
            qemu_log_mask(LOG_GUEST_ERROR, "ipod_video_vc: channel %d message "
                          "without the magic; dropping the ring\n", ch);
            rd = wr;
            break;
        }
        seq    = ldl_le_p(hdr + VC_MSG_OFF_SEQ);
        opcode = ldl_le_p(hdr + VC_MSG_OFF_OPCODE);
        len    = lduw_le_p(hdr + VC_MSG_OFF_LENGTH);
        padded = (len + 15) & ~15u;

        /* A message only exists once all of it is there. */
        if (avail < VC_MSG_HDR_LEN + padded) {
            break;
        }
        rd = peek;
        /*
         * Take what fits and step over the rest, so that an oversized message
         * costs us the message and not the ring's alignment.
         */
        if (len > VC_MAX_PAYLOAD) {
            qemu_log_mask(LOG_UNIMP, "ipod_video_vc: channel %d payload of %u "
                          "bytes is longer than we handle\n", ch, len);
            vc_ring_read(s, start, end, &rd, payload, VC_MAX_PAYLOAD);
            vc_ring_read(s, start, end, &rd, NULL, padded - VC_MAX_PAYLOAD);
            len = VC_MAX_PAYLOAD;
        } else {
            vc_ring_read(s, start, end, &rd, payload, padded);
        }

        /*
         * Release the space before answering: the reply goes on the other ring,
         * so the guest can see the read pointer move either way round, and
         * this is the order the real chip would have to use as well.
         */
        vc_st16(s, ctrl + VC_CH_TX_RD, rd);
        vc_dispatch(s, ch, seq, opcode, payload, len);
    }

    vc_st16(s, ctrl + VC_CH_TX_RD, rd);

    /*
     * Acknowledge the host's doorbell bit the same way it acknowledges ours
     * (0x288138): make signal ^ acknowledgement zero again for this channel,
     * so a later 0x28821c toggles rather than finding the bit already set.
     */
    *vc_iface(s, VC_OFF_VC_ACK) =
        (*vc_iface(s, VC_OFF_VC_ACK) & ~(1u << ch))
        | (*vc_iface(s, VC_OFF_HOST_SIG) & (1u << ch));
}

/*
 * Called for every write that lands in chip memory. The one that matters is
 * the host publishing a channel's write pointer -- 0x288834 pushes sixteen
 * bytes to control block +0x20, low half first -- because that is the store
 * that makes a message exist. Acting on it rather than on the doorbell is both
 * earlier and unconditional: 0x288880 only rings the bell when the ring went
 * from empty, crossed three quarters, or the reader caught up.
 */
void ipod_video_vc_wrote(IPodVideoBCMState *s, uint32_t addr, unsigned size)
{
    uint32_t off;
    int ch;

    if (!s->vc_iface_live
        || addr < VC_IFACE_BASE
        || addr - VC_IFACE_BASE >= VC_TX_OFF(0)) {
        return;
    }
    off = addr - VC_IFACE_BASE;

    for (ch = 0; ch < VC_NUM_CHANNELS; ch++) {
        uint32_t wr = VC_CTRL_OFF(ch) + VC_CH_TX_WR;

        if (off < wr + 2 && off + size > wr) {
            vc_pump(s, ch);
            return;
        }
    }
}

/* The doorbell, 0x287ba4: halfword 0x31 to BCM_CONTROL. */
void ipod_video_vc_kick(IPodVideoBCMState *s)
{
    int ch;

    for (ch = 0; ch < VC_NUM_CHANNELS; ch++) {
        vc_pump(s, ch);
    }
}

static void vc_make_channel(IPodVideoBCMState *s, int ch, uint16_t type)
{
    uint32_t ctrl = VC_CTRL_OFF(ch);

    vc_st16(s, ch * 2, ctrl);           /* the offset table 0x2871d4 indexes */

    memset(vc_iface(s, ctrl), 0, VC_CTRL_LEN);
    vc_st16(s, ctrl + VC_CH_TYPE, type);
    vc_st16(s, ctrl + VC_CH_TX_START, VC_TX_OFF(ch));
    vc_st16(s, ctrl + VC_CH_TX_END, VC_TX_OFF(ch) + VC_RING_LEN);
    vc_st16(s, ctrl + VC_CH_RX_START, VC_RX_OFF(ch));
    vc_st16(s, ctrl + VC_CH_RX_END, VC_RX_OFF(ch) + VC_RING_LEN);
    vc_st16(s, ctrl + VC_CH_TX_RD, VC_TX_OFF(ch));
    vc_st16(s, ctrl + VC_CH_TX_WR, VC_TX_OFF(ch));
    vc_st16(s, ctrl + VC_CH_RX_RD, VC_RX_OFF(ch));
    vc_st16(s, ctrl + VC_CH_RX_WR, VC_RX_OFF(ch));
}

/*
 * Build the interface in chip memory. Called when the doorbell says the
 * VideoCore firmware has started, because that is when a real one would have
 * had a chance to lay it out.
 */
void ipod_video_vc_publish(IPodVideoBCMState *s)
{
    memset(vc_iface(s, 0), 0, VC_TX_OFF(0) + VC_NUM_CHANNELS * 0x1000);

    vc_make_channel(s, VC_CH_GENCMD, VC_TYPE_GENCMD);
    vc_make_channel(s, VC_CH_GFX, VC_TYPE_GFX);

    /* Nothing is pending in either direction yet. */
    vc_st32(s, VC_OFF_HOST_SIG, 0);
    vc_st32(s, VC_OFF_VC_ACK, 0);

    s->vc_iface_live = true;
}

void ipod_video_vc_reset(IPodVideoBCMState *s)
{
    memset(s->surface, 0, sizeof(s->surface));
    s->vc_heap = VC_SURFACE_HEAP;
    s->vc_iface_live = false;
}
