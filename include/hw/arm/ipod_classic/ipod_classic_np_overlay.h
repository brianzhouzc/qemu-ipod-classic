/*
 * HLE "Now Playing" host overlay for the iPod classic 6G retailOS.
 * See hw/arm/ipod_classic/ipod_classic_np_overlay.c. Entirely gated by the
 * HLE_NP_OVERLAY environment variable; a default build installs nothing.
 */
#ifndef IPOD_CLASSIC_NP_OVERLAY_H
#define IPOD_CLASSIC_NP_OVERLAY_H

#include "qemu/osdep.h"

/* Arm the overlay (no-op unless HLE_NP_OVERLAY is set). Call once at machine init. */
void ipod_classic_np_overlay_init(void);

/* True while the overlay should be painted (enabled AND visible). The LCD uses
 * this to keep the console dirty so the clock ticks. */
bool ipod_classic_np_overlay_active(void);

/* Composite the player over the 16bpp (RGB565) framebuffer, AFTER the guest
 * layers. No-op unless active. */
void ipod_classic_np_overlay_draw(uint16_t *fb, int w, int h);

/* Forward a raw keyboard scancode from the wheel handler. No-op unless enabled. */
void ipod_classic_np_overlay_key(int keycode);

/* The I2S transmitter started (a song began) -- auto-show the overlay. */
void ipod_classic_np_overlay_notify_play_start(void);

#endif /* IPOD_CLASSIC_NP_OVERLAY_H */
