/*
 * HLE "Now Playing" host overlay -- iPod classic 6G (S5L8702) retailOS.
 *
 * WHY. wiki/19 s11.12-11.22 proved the retailOS MeCCA audio pipeline is a
 * structural bootstrap deadlock: the guest's own Now Playing render never
 * presents (copy32 0x08016eb4 stays 0) and its elapsed-time counter is fed by
 * the dead decode loop. The data-path HLE (ipod_classic_hle_audio.c) still makes
 * the REAL song play out the guest's own I2S. This module draws a Now Playing
 * player UI directly over the panel framebuffer and drives it from the data-path
 * cursor, so the user gets a visible, controllable player (title, elapsed /
 * remaining, progress bar, play/pause) with working PLAY/PAUSE, seek and skip --
 * the audio staying the guest's own real song and following every control.
 *
 * It is a host overlay, honestly not the pixel-accurate retailOS screen (a
 * monospace bitmap font, no album art), but the clock/progress are EXACT (driven
 * by real PCM consumption) and the controls genuinely move the audio.
 *
 * Gated by HLE_NP_OVERLAY; unset => installs nothing, draws nothing, and the
 * lcd/wheel forwarders are no-ops (byte-identical default build). Needs
 * HLE_AUDIO_DATAPATH (audio+position+track) and HLE_AUDIO_SUSTAIN (so the guest
 * watchdog does not reboot mid-song).
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "ui/vgafont.h"
#include "hw/arm/ipod_classic/ipod_classic_np_overlay.h"
#include "hw/arm/ipod_classic/ipod_classic_hle_audio.h"

/* Keyboard scancodes as delivered to ipod_classic_wheel.c's kbd handler. */
#define SC_A      0x1e   /* LEFT  / prev  */
#define SC_S      0x1f   /* PLAY / pause  */
#define SC_D      0x20   /* RIGHT / next  */
#define SC_W      0x11   /* MENU  / hide  */
#define SC_ENTER  0x1c   /* SELECT / show */
#define SC_LBRK   0x1a   /* '[' seek back */
#define SC_RBRK   0x1b   /* ']' seek fwd  */

typedef struct {
    bool enabled;
    bool visible;
    bool user_hid;   /* user pressed MENU to reveal the guest UI underneath */
} NPOverlay;

static NPOverlay ov;

/* ------------------------------------------------------------ drawing ------- */

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
}

static void fill_rect(uint16_t *fb, int W, int H, int x, int y, int w, int h,
                      uint16_t c)
{
    for (int yy = y; yy < y + h; yy++) {
        if (yy < 0 || yy >= H) {
            continue;
        }
        for (int xx = x; xx < x + w; xx++) {
            if (xx < 0 || xx >= W) {
                continue;
            }
            fb[yy * W + xx] = c;
        }
    }
}

/* 8x16 VGA glyph, scaled by `s`, foreground only (transparent background). */
static void draw_char(uint16_t *fb, int W, int H, int x, int y, unsigned char ch,
                      uint16_t fg, int s)
{
    const uint8_t *g = &vgafont16[ch * 16];
    for (int row = 0; row < 16; row++) {
        uint8_t bits = g[row];
        for (int col = 0; col < 8; col++) {
            if (!(bits & (0x80 >> col))) {
                continue;
            }
            fill_rect(fb, W, H, x + col * s, y + row * s, s, s, fg);
        }
    }
}

static int draw_string(uint16_t *fb, int W, int H, int x, int y,
                       const char *str, uint16_t fg, int s)
{
    for (const char *p = str; *p; p++) {
        draw_char(fb, W, H, x, y, (unsigned char)*p, fg, s);
        x += 8 * s;
    }
    return x;
}

/* Centre a string horizontally at width W, truncating with an ellipsis if it
 * would run off-screen at scale s. */
static void draw_string_centered(uint16_t *fb, int W, int H, int y,
                                 const char *str, uint16_t fg, int s)
{
    char buf[64];
    int maxchars = (W - 4) / (8 * s);
    int len = (int)strlen(str);
    int x;

    if (len > maxchars && maxchars > 2) {
        int keep = maxchars - 2;
        if (keep > (int)sizeof(buf) - 3) {
            keep = (int)sizeof(buf) - 3;
        }
        memcpy(buf, str, keep);
        buf[keep] = '.';
        buf[keep + 1] = '.';
        buf[keep + 2] = '\0';
        str = buf;
        len = keep + 2;
    }
    x = (W - len * 8 * s) / 2;
    if (x < 2) {
        x = 2;
    }
    draw_string(fb, W, H, x, y, str, fg, s);
}

static void fmt_time(char *buf, size_t n, uint64_t frames)
{
    uint64_t secs = frames / 44100;
    snprintf(buf, n, "%u:%02u", (unsigned)(secs / 60), (unsigned)(secs % 60));
}

/* ------------------------------------------------------------ public -------- */

bool ipod_classic_np_overlay_active(void)
{
    if (!ov.enabled) {
        return false;
    }
    /* Auto-show once a real song has been decoded (have_pcm), unless the user
     * pressed MENU to reveal the guest UI. Until then the guest menu stays
     * visible so the user can navigate and pick a song. */
    if (ipod_classic_hle_audio_have_pcm() && !ov.user_hid) {
        ov.visible = true;
    }
    return ov.visible;
}

void ipod_classic_np_overlay_draw(uint16_t *fb, int w, int h)
{
    uint16_t bg, fg, dim, barbg, barfg;
    uint64_t pos, total;
    char tel[16], trem[16], idx[24];
    const char *name;
    int y;

    if (!ov.enabled || !ov.visible || !fb) {
        return;
    }

    bg    = rgb565(0x10, 0x12, 0x18);
    fg    = rgb565(0xf0, 0xf0, 0xf0);
    dim   = rgb565(0x90, 0x98, 0xa4);
    barbg = rgb565(0x3a, 0x3e, 0x48);
    barfg = rgb565(0x3d, 0x9b, 0xff);

    /* Full-screen background (occludes the frozen guest menu underneath). */
    fill_rect(fb, w, h, 0, 0, w, h, bg);

    /* Top bar. */
    fill_rect(fb, w, h, 0, 0, w, 22, rgb565(0x24, 0x28, 0x30));
    draw_string(fb, w, h, 6, 3, "Now Playing", fg, 1);
    draw_string(fb, w, h, w - 8 * 8 - 6, 3,
                ipod_classic_hle_audio_paused() ? " PAUSED" : "PLAYING", dim, 1);

    /* Track title (2x) -- real tag if present, else the file name. */
    name = ipod_classic_hle_audio_track_title();
    if (!name || !*name) {
        name = ipod_classic_hle_audio_track_name();
    }
    if (!name || !*name) {
        name = "Track";
    }
    draw_string_centered(fb, w, h, 60, name, fg, 2);

    /* Artist / album (real tags if present). */
    {
        const char *artist = ipod_classic_hle_audio_track_artist();
        const char *album  = ipod_classic_hle_audio_track_album();
        if (artist && *artist) {
            draw_string_centered(fb, w, h, 92, artist, fg, 1);
        }
        if (album && *album) {
            draw_string_centered(fb, w, h, 106, album, dim, 1);
        }
    }

    snprintf(idx, sizeof(idx), "Track %d of %d",
             ipod_classic_hle_audio_track_index() + 1,
             ipod_classic_hle_audio_track_count());
    draw_string_centered(fb, w, h, 124, idx, dim, 1);

    /* Progress bar + times. */
    pos   = ipod_classic_hle_audio_position_frames();
    total = ipod_classic_hle_audio_total_frames();
    {
        int bx = 24, bw = w - 48, by = 176, bh = 8;
        fill_rect(fb, w, h, bx, by, bw, bh, barbg);
        if (total > 0) {
            int fillw = (int)((uint64_t)bw * pos / total);
            if (fillw > bw) {
                fillw = bw;
            }
            fill_rect(fb, w, h, bx, by, fillw, bh, barfg);
            /* knob */
            fill_rect(fb, w, h, bx + fillw - 2, by - 2, 4, bh + 4, fg);
        }
        fmt_time(tel, sizeof(tel), pos);
        fmt_time(trem, sizeof(trem), total > pos ? total - pos : 0);
        draw_string(fb, w, h, bx, by + 14, tel, dim, 1);
        {
            char neg[18];
            snprintf(neg, sizeof(neg), "-%s", trem);
            draw_string(fb, w, h, bx + bw - (int)strlen(neg) * 8, by + 14,
                        neg, dim, 1);
        }
    }

    /* Hint line. */
    y = h - 16;
    draw_string_centered(fb, w, h, y,
                         "s:play/pause  a/d:prev/next  [ ]:seek  w:menu", dim, 1);
}

void ipod_classic_np_overlay_key(int keycode)
{
    int sc = keycode & 0x7f;
    bool down = !(keycode & 0x80);   /* bit7 set = key-up */

    if (!ov.enabled || !down) {
        return;
    }
    switch (sc) {
    case SC_ENTER:                       /* SELECT: show the player */
        ov.visible = true;
        ov.user_hid = false;
        break;
    case SC_W:                           /* MENU: hide (reveal guest menu) */
        ov.visible = false;
        ov.user_hid = true;
        break;
    default:
        break;
    }
    if (!ov.visible) {
        return;                          /* controls act only while shown */
    }
    switch (sc) {
    case SC_S:                           /* PLAY/PAUSE */
        ipod_classic_hle_audio_toggle_pause();
        break;
    case SC_D:                           /* next track */
        ipod_classic_hle_audio_change_track(+1);
        break;
    case SC_A:                           /* prev track */
        ipod_classic_hle_audio_change_track(-1);
        break;
    case SC_RBRK:                        /* seek forward 5s */
        ipod_classic_hle_audio_seek(+5);
        break;
    case SC_LBRK:                        /* seek back 5s */
        ipod_classic_hle_audio_seek(-5);
        break;
    default:
        break;
    }
}

void ipod_classic_np_overlay_notify_play_start(void)
{
    /* Do NOT show here: this edge also fires for the boot silence init, which
     * would cover the menu before a song is picked. The overlay auto-shows in
     * _active() once a track is actually decoded (have_pcm). */
}

void ipod_classic_np_overlay_init(void)
{
    const char *e = getenv("HLE_NP_OVERLAY");
    if (!e || !*e || *e == '0') {
        return;   /* default build: install nothing */
    }
    ov.enabled = true;
    ov.visible = false;
    warn_report("HLE_NP_OVERLAY: host Now Playing overlay armed "
                "(s=play/pause a/d=prev/next [ ]=seek w=menu enter=show)");
}
