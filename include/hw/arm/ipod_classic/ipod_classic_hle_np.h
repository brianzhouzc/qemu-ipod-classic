/*
 * HLE "Now Playing" bootstrap for the iPod classic 6G retailOS.
 * See hw/arm/ipod_classic/ipod_classic_hle_np.c and wiki/19 section 11.20.
 * Entirely gated by the HLE_NP environment variable; a default build installs
 * nothing.
 */
#ifndef IPOD_CLASSIC_HLE_NP_H
#define IPOD_CLASSIC_HLE_NP_H

#include "target/arm/cpu.h"

/* Arm the bootstrap (no-op unless HLE_NP is set). Call once at machine init. */
void ipod_classic_hle_np_init(ARMCPU *cpu);

/* The I2S transmitter's run bit went 1 -- a reliable per-play "audio starting"
 * edge. Starts the retry timer. No-op unless armed. */
void ipod_classic_hle_np_notify_play_start(void);

#endif /* IPOD_CLASSIC_HLE_NP_H */
