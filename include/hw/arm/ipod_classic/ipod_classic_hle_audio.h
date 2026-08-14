/*
 * HLE audio "kick" shim for the iPod classic 6G retailOS. See
 * hw/arm/ipod_classic/ipod_classic_hle_audio.c and wiki/19 section 11 (11.15).
 *
 * Gated entirely by the HLE_AUDIO_KICK environment variable: when it is unset
 * (the default) ipod_classic_hle_audio_init() installs nothing and the machine
 * is byte-for-byte identical to the baseline.
 */
#ifndef IPOD_CLASSIC_HLE_AUDIO_H
#define IPOD_CLASSIC_HLE_AUDIO_H

#include "cpu.h"

void ipod_classic_hle_audio_init(ARMCPU *cpu);

/*
 * Called by the I2S model when the transmitter's run bit goes 1 -- the reliable
 * "audio is starting" signal that arms the kick. A no-op unless HLE_AUDIO_KICK is
 * set, so wiring it in costs nothing in the default build.
 */
void ipod_classic_hle_audio_notify_play_start(void);

#endif /* IPOD_CLASSIC_HLE_AUDIO_H */
