/*
 * HLE audio "kick" shim -- iPod classic 6G (S5L8702) retailOS.
 *
 * WHAT THIS IS (and is not)
 * -------------------------
 * A High-Level Emulation shim that emits the "type-12 render-pull" inter-task
 * message the prior sessions identified as missing from the retailOS audio
 * pipeline at cold start. It does NOT inject PCM and it does NOT patch firmware:
 * it only delivers the message a real device's completion would have caused --
 * the same spirit as HLE-ing the 5G VideoCore (model what a mechanism *does*, do
 * not fake its output). Everything is gated by the environment variable
 * HLE_AUDIO_KICK; with it unset the machine installs nothing and is byte-for-byte
 * the baseline (Rockbox and navtest are untouched).
 *
 * HONEST STATUS (wiki/19 section 11.15 -- read it before trusting the framing)
 * ---------------------------------------------------------------------------
 * This shim is IMPLEMENTED and its delivery MECHANISM is VALIDATED (it fires the
 * guest dispatcher from the idle context; traces "emitted type-12 ..."). But it
 * does NOT yet make the retailOS produce audio, and the measurement shows why the
 * prior sessions' framing was incomplete:
 *   - The decoder DOES prime: with a RELIABLE SELECT the render-pull gate
 *     [[obj+0x34]+0x60] reads 0x4 (non-zero). The "gate never set" reading was a
 *     regime-B artefact of under-navigation.
 *   - The type-12 IS emitted -- by the FIRMWARE ITSELF (measured h12=1,
 *     gate-consume 0x081239bc=1, dwork=3) once the play truly progresses. The
 *     "h12=0, nobody emits it" of sections 11.13/11.14 was also under-navigation.
 *   - Yet copy32 (0x08016eb4) stays 0 and the mixer PCM source gets ONLY memset
 *     writes: the type-12 render-pull is NOT sufficient. dwork (0x08123bfc) does
 *     not decode inline; it POSTS an async decode message to obj+0x54, and that
 *     downstream AAC-decode task never produces PCM. THE REAL WALL IS ONE LEVEL
 *     BELOW the type-12 -- so this shim's kick is (currently) redundant with the
 *     firmware's own and equally silent.
 * Kept in the tree as validated, reusable delivery infrastructure (idle-context
 * redirect to a guest function without disturbing any live task) and as the
 * anchor for the corrected wiki analysis; NOT as a working audio path.
 *
 * WHAT WE EMIT
 * ------------
 * The type-12 render-pull, exactly as the dispatcher expects it:
 *   msg[4] = 12                (dispatcher 0x08123dcc switches on [msg+4])
 *   msg[8] = decoder_obj+0x54  (handler 0x08123970 requires [msg+8]==obj+0x54)
 * The handler checks/consumes the gate [[obj+0x34]+0x60] and calls the decode
 * 0x08123bfc, which posts the async decode chain (see HONEST STATUS above).
 *
 * HOW WE EMIT IT (mechanism)
 * --------------------------
 * The I2S transmitter's run bit going 1 (every play) arms a short virtual-clock
 * retry timer. On each retry we locate the decoder object (measured hint
 * 0x0ba9a8a4, validated by its vtable word; a one-shot heap scan is the fallback)
 * and read its render-pull gate; when the gate is non-zero (primed) and the CPU
 * is at the RTXC idle loop / halted in WFI (so no live task's context is
 * disturbed), we redirect the idle context to call the guest's own dispatcher
 * 0x08123dcc(decoder_obj, msg) with a 12-byte type-12 message written just below
 * the idle stack and the return address pointing back into the idle loop. The
 * dispatcher runs the guest's own handler; the woken work is picked up by the
 * next 1 kHz scheduler tick. Nothing is faked; we only ring the doorbell.
 *
 * Addresses are VERIFIED from the osos disassembly (VA = file off + 0x07FF51B8)
 * and non-invasive gdb/pctrace reads of a live play (wiki/19 11.15).
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "hw/core/cpu.h"
#include "exec/cpu-common.h"
#include "hw/arm/ipod_classic/ipod_classic_hle_audio.h"

/* -------- VERIFIED guest addresses (osos VA = off + 0x07FF51B8) ----------- */

/* Decoder actor vtable word, stored at [decoder_obj+0] by ctor 0x08123ee8. */
#define HLE_DECODER_VTABLE      0x088d1948u
/* Measured decoder_obj on a deterministic Emulti.img play (wiki/19 11.15). The
 * boot is deterministic so this validates directly; a bounded one-shot scan is
 * the fallback if it ever moves. Cheap probing here is essential -- a repeated
 * multi-MB heap scan starves the vCPU of host time and pushes the guest into the
 * under-progressed "regime B" where the decoder never primes at all. */
#define HLE_DECODER_OBJ_HINT    0x0ba9a8a4u
/* Heap window that holds the MeCCA actor objects (measured 0x0ba9xxxx). */
#define HLE_HEAP_SCAN_LO        0x0b400000u
#define HLE_HEAP_SCAN_HI        0x0bc00000u
/* Decoder dispatcher: switch on [msg+4]; type 12 -> 0x08123970 (decode pull). */
#define HLE_DECODER_DISPATCH    0x08123dccu
/* Decoder object field offsets. */
#define HLE_OBJ_CTX_OFF         0x34   /* [obj+0x34] -> ctx */
#define HLE_CTX_GATE_OFF        0x60   /* [ctx+0x60] == render-pull gate (!=0 ready) */
#define HLE_OBJ_DECPORT_OFF     0x54   /* handler-12 compares [msg+8] to obj+0x54 */
/* RTXC idle loop (WFI); the CPU sits here through the deadlock. */
#define HLE_IDLE_LO             0x22003b00u
#define HLE_IDLE_HI             0x22003b40u
#define HLE_IDLE_ENTRY          0x22003b0cu

/* Retry cadence: fast, to catch the brief window in which the render-pull gate
 * is set before the firmware (in a self-progressing run) consumes it. */
#define HLE_FIRST_MS            100
#define HLE_RETRY_MS            10
#define HLE_MAX_ATTEMPTS        400    /* 100 + 400*10 ms ~= 4 s */
#define HLE_MAX_KICKS           1      /* one render-pull is enough to prime */
/*
 * Demonstration fallback. If HLE_AUDIO_KICK_FORCE is set and the gate window was
 * never caught, kick once anyway at this attempt (object still validated) so the
 * delivery mechanism can be exercised end to end. With gate==0 the guest handler
 * runs but skips the decode, so this proves delivery without forging any state.
 */
#define HLE_FORCE_AT            60     /* ~= I2S-run + 0.7 s */

/* --------------------------------------------------------------- state ---- */

typedef struct {
    ARMCPU    *cpu;
    QEMUTimer *timer;
    bool       enabled;
    bool       trace;
    bool       force;         /* HLE_AUDIO_KICK_FORCE: demo kick even if gate==0 */
    bool       armed;         /* retry timer running */
    bool       scanned;       /* one-shot heap scan already tried this arming */
    bool       forced;        /* the demo kick was scheduled gate-independently */
    int        attempts;
    int        kicks;
    uint32_t   decoder_obj;   /* cached, validated decoder object */
    uint32_t   primed_obj;    /* decoder object with a live gate, for the kick */
} HLEAudioState;

static HLEAudioState hle;

/* --------------------------------------------------------------- helpers -- */

static uint32_t hle_rd32(uint32_t addr)
{
    uint32_t v = 0;
    cpu_physical_memory_read(addr, &v, sizeof(v));
    return v;   /* device is little-endian like the guest */
}

static void hle_wr32(uint32_t addr, uint32_t v)
{
    cpu_physical_memory_write(addr, &v, sizeof(v));
}

/* The render-pull gate for a decoder object, or 0 if not readable/primed. */
static uint32_t hle_gate(uint32_t obj)
{
    uint32_t ctx = hle_rd32(obj + HLE_OBJ_CTX_OFF);
    if (ctx < 0x08000000u) {
        return 0;   /* ctx not wired yet */
    }
    return hle_rd32(ctx + HLE_CTX_GATE_OFF);
}

/* One bounded heap scan for the decoder object (vtable 0x088d1948). Run at most
 * once per arming -- it is the fallback for when the measured hint has moved. */
static uint32_t hle_scan_decoder(void)
{
    static uint8_t buf[0x10000];   /* single-threaded use; keep off the stack */
    for (uint32_t base = HLE_HEAP_SCAN_LO; base < HLE_HEAP_SCAN_HI;
         base += sizeof(buf)) {
        cpu_physical_memory_read(base, buf, sizeof(buf));
        for (uint32_t off = 0; off + 4 <= sizeof(buf); off += 4) {
            uint32_t w = buf[off] | (buf[off + 1] << 8) |
                         (buf[off + 2] << 16) | (buf[off + 3] << 24);
            if (w == HLE_DECODER_VTABLE) {
                return base + off;
            }
        }
    }
    return 0;
}

/*
 * Return the decoder object if it is primed right now (gate != 0), else 0. This
 * must be CHEAP: it validates the measured hint (a couple of word reads) and only
 * falls back to a single heap scan the first time the hint fails, caching the
 * result. `gate_out` receives the gate value for tracing.
 */
static uint32_t hle_find_primed_decoder(uint32_t *gate_out)
{
    uint32_t gate;

    if (!hle.decoder_obj || hle_rd32(hle.decoder_obj) != HLE_DECODER_VTABLE) {
        hle.decoder_obj = (hle_rd32(HLE_DECODER_OBJ_HINT) == HLE_DECODER_VTABLE)
                          ? HLE_DECODER_OBJ_HINT : 0;
        if (!hle.decoder_obj && !hle.scanned) {
            hle.scanned = true;                 /* at most one scan per arming */
            hle.decoder_obj = hle_scan_decoder();
        }
    }
    if (!hle.decoder_obj) {
        *gate_out = 0;
        return 0;
    }
    gate = hle_gate(hle.decoder_obj);
    *gate_out = gate;
    return gate ? hle.decoder_obj : 0;
}

/* --------------------------------------------------- the kick (vCPU thread) */
/*
 * Runs on the vCPU thread at a safe point (async_run_on_cpu). Fires the doorbell
 * only when the CPU is idle so no live task's register/stack context is
 * disturbed; otherwise it defers back to the retry timer.
 */
static void hle_do_kick(CPUState *cs, run_on_cpu_data data)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    uint32_t obj = hle.primed_obj;
    uint32_t pc = env->regs[15];
    bool idle = cs->halted || (pc >= HLE_IDLE_LO && pc < HLE_IDLE_HI);
    uint32_t sp, msg, ret;

    if (hle.kicks >= HLE_MAX_KICKS) {
        return;
    }
    /* Re-validate on the vCPU thread: object still a real decoder? The gate must
     * be live unless this is the gate-independent demonstration kick. */
    if (!obj || hle_rd32(obj) != HLE_DECODER_VTABLE) {
        return;
    }
    if (!hle.forced && hle_gate(obj) == 0) {
        return;   /* window moved/closed; the timer will re-scan */
    }
    if (!idle) {
        return;   /* a real task is running; wait for the idle loop */
    }

    /*
     * Build the type-12 message in scratch below the idle stack. Placing it at
     * sp-0x100 while dropping sp to sp-0x200 keeps it above the dispatcher's own
     * frames (which grow below the new sp), so the guest call never clobbers it.
     * We do not restore sp: the idle task tolerates the shift and simply loops.
     */
    sp  = env->regs[13];
    msg = sp - 0x100;
    ret = (pc >= HLE_IDLE_LO && pc < HLE_IDLE_HI) ? pc : HLE_IDLE_ENTRY;

    hle_wr32(msg + 0, 0);
    hle_wr32(msg + 4, 12);                        /* dispatcher type = 12 */
    hle_wr32(msg + 8, obj + HLE_OBJ_DECPORT_OFF); /* == obj+0x54 (handler gate) */
    hle_wr32(msg + 12, 0);

    env->regs[0]  = obj;                          /* r0 = decoder_obj */
    env->regs[1]  = msg;                          /* r1 = message */
    env->regs[13] = sp - 0x200;                   /* headroom for the call */
    env->regs[14] = ret;                          /* LR -> back to idle loop */
    env->thumb    = false;                        /* dispatcher is ARM code */
    env->regs[15] = HLE_DECODER_DISPATCH;         /* PC -> 0x08123dcc */
    cs->halted    = 0;                            /* leave WFI, run the call */

    hle.kicks++;
    fprintf(stderr, "HLE_AUDIO_KICK: emitted type-12 render-pull to decoder "
            "obj=0x%08x gate=0x%08x (msg=0x%08x [msg+8]=0x%08x) from idle "
            "pc=0x%08x\n", obj, hle_gate(obj), msg, obj + HLE_OBJ_DECPORT_OFF,
            pc);
}

/* ---------------------------------------------------- the retry (iothread) - */

static void hle_attempt(void *opaque)
{
    uint32_t gate = 0;

    if (hle.kicks >= HLE_MAX_KICKS) {
        hle.armed = false;
        return;
    }

    hle.primed_obj = hle_find_primed_decoder(&gate);

    /* Demonstration fallback: if the live-gate window was never caught, kick the
     * validated object once anyway so the delivery path is exercised. */
    if (!hle.primed_obj && hle.force && hle.decoder_obj &&
        hle.attempts == HLE_FORCE_AT) {
        hle.primed_obj = hle.decoder_obj;
        hle.forced = true;
    }

    if (hle.trace && (hle.attempts % 8 == 0 || hle.primed_obj)) {
        fprintf(stderr, "HLE_AUDIO_KICK attempt %d: obj=0x%08x gate=0x%08x%s\n",
                hle.attempts, hle.decoder_obj, gate,
                hle.forced ? " (FORCED demo kick)" : "");
    }
    if (hle.primed_obj) {
        async_run_on_cpu(CPU(hle.cpu), hle_do_kick, RUN_ON_CPU_NULL);
    }

    if (++hle.attempts < HLE_MAX_ATTEMPTS) {
        timer_mod(hle.timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                  HLE_RETRY_MS * 1000000LL);
    } else {
        hle.armed = false;
    }
}

/*
 * Called by the I2S model when the transmitter's run bit goes 1 -- the reliable,
 * every-play "audio is starting" signal. Arms the retry timer once; if a play
 * retries and we have not kicked yet, it re-arms.
 */
void ipod_classic_hle_audio_notify_play_start(void)
{
    if (!hle.enabled || hle.armed || hle.kicks >= HLE_MAX_KICKS) {
        return;
    }
    hle.armed = true;
    hle.attempts = 0;
    hle.scanned = false;
    hle.forced = false;
    timer_mod(hle.timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + HLE_FIRST_MS * 1000000LL);
    if (hle.trace) {
        fprintf(stderr, "HLE_AUDIO_KICK: play start seen, retry timer armed\n");
    }
}

/* --------------------------------------------------------------- init ----- */

void ipod_classic_hle_audio_init(ARMCPU *cpu)
{
    const char *e = getenv("HLE_AUDIO_KICK");

    if (!e || !*e || *e == '0') {
        return;   /* default build: install nothing, byte-identical baseline */
    }

    memset(&hle, 0, sizeof(hle));
    hle.cpu = cpu;
    hle.enabled = true;
    e = getenv("HLE_AUDIO_KICK_TRACE");
    hle.trace = (e && *e && *e != '0');
    e = getenv("HLE_AUDIO_KICK_FORCE");
    hle.force = (e && *e && *e != '0');

    hle.timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, hle_attempt, &hle);

    fprintf(stderr, "HLE_AUDIO_KICK: armed (waits for I2S run; decoder vtable "
            "0x%08x)\n", HLE_DECODER_VTABLE);
}
