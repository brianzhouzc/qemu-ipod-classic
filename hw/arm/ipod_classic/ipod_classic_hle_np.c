/*
 * HLE "Now Playing" bootstrap -- iPod classic 6G (S5L8702) retailOS.
 *
 * GOAL (wiki/19 section 11.20). Break the symmetric MeCCA audio bootstrap
 * deadlock so the guest's OWN decoder produces PCM and the guest's OWN engine
 * renders it (copy 0x08016eb4 > 0) -- which advances the Now Playing UI. This is
 * NOT the data-path HLE (ipod_classic_hle_audio.c, which feeds host-decoded PCM
 * downstream of the dead engine); here the guest engine does the rendering.
 *
 * WHY the prior 11 sessions' kicks failed (measured this session):
 *   - The decoder's publish (0x081c12ac) does NOT write the engine's copy
 *     descriptor directly; it POSTS the frame as a message (0x832d9dc) to a
 *     downstream port. The engine (t25) is parked on a payload-less doorbell
 *     (fast-sema 0x220106d0); ringing the doorbell only wakes it -- it re-polls
 *     its copy source, finds it EMPTY (no message delivered the binding), and
 *     re-parks. So kicking the doorbell never renders.
 *   - The engine's copy 0x08016e44 reads PCM from src = [r5+12] (= 0x08889098,
 *     measured live) gated by [r7+5]==1 (r7 = 0x0bb12eb4, measured live [+5]=0).
 *     In cold start the src holds no PCM and the gate is 0.
 *
 * WHAT WE DO (authorized: prime CONTROL bindings; PCM stays the guest decoder's).
 *   From the RTXC idle loop (WFI, so no live task's context is disturbed):
 *     (1) DECODER: run the guest's own decode-a-frame case 0x081232d8(obj,0),
 *         which decodes frame N with the guest AAC math into the reader's buffer.
 *         r1=0 makes the post-decode skip the blocking input dstep.
 *     (2) BIND: point the engine's copy source at the freshly decoded PCM and
 *         open the copy gate -- the exact binding the engine's message handler
 *         would set on receiving the publish message. NO PCM is fabricated; the
 *         samples are the guest decoder's own output.
 *     (3) MOTOR: arm [engobj+71]=1 and ring the doorbell (guest stub 0x22006870)
 *         so t25 wakes and its OWN copy/mixer renders the now-bound PCM.
 *
 * All gated by HLE_NP; unset => this file installs nothing (byte-identical
 * baseline; Rockbox/navtest untouched). Sub-knobs:
 *   HLE_NP_DEC_ONLY   only kick the decoder (P-finding with pctrace memw)
 *   HLE_NP_SRC=0x..   override the copy source buffer address (default measured)
 *   HLE_NP_GATE=0x..  override the copy gate byte address (default measured)
 *   HLE_NP_TRACE      per-kick trace
 *
 * Addresses VERIFIED from a live non-invasive snapshot of a reliably navigated
 * play (wiki/19 11.20; disk Emulti.img): decoder_obj=0x0ba9a8a4 vt=0x088d1948
 * ctx=[obj+0x34] gate[ctx+0x60]=0x4 reader=[ctx+0x48] decode-step=0x081232d8;
 * copy src descr [0x088890cc]=0x08889098; copy gate [0x0bb12eb9].
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "hw/core/cpu.h"
#include "exec/cpu-common.h"
#include "target/arm/cpu.h"
#include "hw/arm/ipod_classic/ipod_classic_hle_np.h"

/* --------------- VERIFIED guest addresses (osos VA = off + 0x07FF51B8) ------ */

#define NP_DECODER_OBJ      0x0ba9a8a4u
#define NP_DECODER_VTABLE   0x088d1948u
#define NP_DEC_CTX_OFF      0x34u   /* [obj+0x34] = decode ctx                  */
#define NP_DEC_OBJ56_OFF    0x38u   /* decode gate: must be 0 (0x81232d8)       */
#define NP_DEC_OBJ132_OFF   0x84u   /* decode gate: must be 0 (0x81232d8)       */
#define NP_DEC_CTX_GATE_OFF 0x60u   /* gate[ctx+0x60] != 0  <=>  primed (0x4)   */
#define NP_DEC_CTX_READER   0x48u   /* [ctx+0x48] = reader                      */
#define NP_DECODE_STEP      0x081232d8u  /* decode-a-frame case (obj, 0)         */

/* Engine copy binding (measured live at the bring-up render). */
#define NP_COPY_SRC_DESCR   0x088890c0u  /* r5; source base at [descr+12]        */
#define NP_COPY_SRC_OFF     12u
#define NP_COPY_SRC_BUF     0x08889098u  /* = [0x088890cc]; where copy reads PCM  */
#define NP_COPY_GATE_BYTE   0x0bb12eb9u  /* [r7+5]; must be 1 to copy            */

/* Render doorbell (fast-sema t25 recv()s on) and engine object. */
#define NP_RING_OBJ         0x220106d0u
#define NP_RING_WAITERS_OFF 0x04u
#define NP_ENGOBJ           0x220106dcu
#define NP_ENG_GATE_OFF     71u
#define NP_SIGNAL_STUB      0x22006870u  /* r0=ring;r1=0;b V()  (pure doorbell)  */

/* RTXC idle loop. */
#define NP_IDLE_LO          0x22003b0cu
#define NP_IDLE_HI          0x22003b34u
#define NP_IDLE_ENTRY       0x22003b0cu

#define NP_FIRST_MS         100
#define NP_RETRY_MS         5
#define NP_MAX_KICKS        4000000

/* --------------------------------------------------------------- state ----- */

typedef struct {
    ARMCPU    *cpu;
    QEMUTimer *timer;
    bool       enabled;
    bool       trace;
    bool       dec_only;
    bool       armed;
    bool       pending;
    bool       want_bind;     /* alternation: false=decode next, true=bind+motor */
    uint32_t   src_buf;       /* copy source buffer (HLE_NP_SRC or default)      */
    uint32_t   gate_byte;     /* copy gate byte addr (HLE_NP_GATE or default)    */
    int        attempts;
    int        kicks;
    int        dec_kicks;
    int        bind_kicks;
    int        deferred;
} NPState;

static NPState np;

/* --------------------------------------------------------------- helpers ---- */

static uint32_t np_rd32(uint32_t a)
{
    uint32_t v = 0;
    cpu_physical_memory_read(a, &v, sizeof(v));
    return v;
}
static void np_wr32(uint32_t a, uint32_t v)
{
    cpu_physical_memory_write(a, &v, sizeof(v));
}
static void np_wr8(uint32_t a, uint8_t v)
{
    cpu_physical_memory_write(a, &v, sizeof(v));
}

static uint32_t np_ring_waiter(void)
{
    uint32_t w = np_rd32(NP_RING_OBJ + NP_RING_WAITERS_OFF);
    return (w >= 0x08000000u) ? w : 0;
}

/* The decode ctx if the decoder is constructed and its decode-a-frame gate is
 * open (the exact preconditions 0x81232d8 checks), else 0. Reads only. */
static uint32_t np_decoder_ctx(void)
{
    uint32_t ctx;

    if (np_rd32(NP_DECODER_OBJ) != NP_DECODER_VTABLE) {
        return 0;
    }
    if (np_rd32(NP_DECODER_OBJ + NP_DEC_OBJ56_OFF) != 0 ||
        np_rd32(NP_DECODER_OBJ + NP_DEC_OBJ132_OFF) != 0) {
        return 0;
    }
    ctx = np_rd32(NP_DECODER_OBJ + NP_DEC_CTX_OFF);
    if (ctx < 0x08000000u) {
        return 0;
    }
    if (np_rd32(ctx + NP_DEC_CTX_GATE_OFF) == 0) {
        return 0;
    }
    return ctx;
}

/* --------------------------------------------------- the kick (vCPU thread) - */

static void np_do_kick(CPUState *cs, run_on_cpu_data data)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    uint32_t pc = env->regs[15];
    bool idle = cs->halted || (pc >= NP_IDLE_LO && pc < NP_IDLE_HI);
    uint32_t ret, ctx, waiter;

    np.pending = false;
    if (np.kicks >= NP_MAX_KICKS) {
        return;
    }
    if (!idle) {
        np.deferred++;
        return;
    }
    ret = (pc >= NP_IDLE_LO && pc < NP_IDLE_HI) ? pc : NP_IDLE_ENTRY;

    ctx = np_decoder_ctx();
    waiter = np_ring_waiter();

    if (np.dec_only) {
        if (ctx == 0) {
            return;
        }
        /* Decoder only: run the guest decode-a-frame; measure where PCM lands. */
        env->regs[0]  = NP_DECODER_OBJ;
        env->regs[1]  = 0;
        env->regs[14] = ret;
        env->thumb    = false;
        env->regs[15] = NP_DECODE_STEP;
        cs->halted    = 0;
        np.dec_kicks++;
        np.kicks++;
        if (np.trace && np.dec_kicks <= 8) {
            uint32_t reader = np_rd32(ctx + NP_DEC_CTX_READER);
            fprintf(stderr, "HLE_NP: DECODE #%d ctx=0x%08x reader=0x%08x "
                    "srcbuf[%08x]=%08x from idle pc=0x%08x\n",
                    np.dec_kicks, ctx, reader, np.src_buf,
                    np_rd32(np.src_buf), pc);
        }
        return;
    }

    /*
     * Full mode: alternate decode / (bind + motor doorbell).
     * Decode-first so a frame exists before we bind and ring.
     */
    if (!np.want_bind && ctx != 0) {
        env->regs[0]  = NP_DECODER_OBJ;
        env->regs[1]  = 0;
        env->regs[14] = ret;
        env->thumb    = false;
        env->regs[15] = NP_DECODE_STEP;
        cs->halted    = 0;
        np.dec_kicks++;
        np.kicks++;
        np.want_bind = true;
        if (np.trace && np.dec_kicks <= 8) {
            fprintf(stderr, "HLE_NP: DECODE #%d ctx=0x%08x\n", np.dec_kicks, ctx);
        }
        return;
    }

    if (waiter != 0) {
        /* BIND: open the copy gate so the engine's own copy renders the PCM the
         * guest decoder just produced into the engine's source buffer. (The
         * source-buffer wiring is refined once P is measured; for now open the
         * gate and ring -- this stage is developed iteratively.) */
        np_wr8(np.gate_byte, 1);
        np_wr8(NP_ENGOBJ + NP_ENG_GATE_OFF, 1);
        /* MOTOR: ring the doorbell via the guest's own stub. */
        env->regs[0]  = NP_RING_OBJ;
        env->regs[1]  = 0;
        env->regs[14] = ret;
        env->thumb    = false;
        env->regs[15] = NP_SIGNAL_STUB;
        cs->halted    = 0;
        np.bind_kicks++;
        np.kicks++;
        np.want_bind = false;
        if (np.trace && np.bind_kicks <= 8) {
            fprintf(stderr, "HLE_NP: BIND+MOTOR #%d gate[%08x]=1 waiter=0x%08x\n",
                    np.bind_kicks, np.gate_byte, waiter);
        }
    }
}

/* ---------------------------------------------------- the retry (iothread) -- */

static void np_attempt(void *opaque)
{
    if (np.kicks >= NP_MAX_KICKS) {
        np.armed = false;
        return;
    }
    if (!np.pending && (np_decoder_ctx() || np_ring_waiter())) {
        np.pending = true;
        async_run_on_cpu(CPU(np.cpu), np_do_kick, RUN_ON_CPU_NULL);
    }
    if (np.trace && np.attempts % 400 == 0) {
        fprintf(stderr, "HLE_NP attempt %d: ctx=0x%08x waiter=0x%08x dec=%d "
                "bind=%d deferred=%d\n", np.attempts, np_decoder_ctx(),
                np_ring_waiter(), np.dec_kicks, np.bind_kicks, np.deferred);
    }
    np.attempts++;
    timer_mod(np.timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
              NP_RETRY_MS * 1000000LL);
}

void ipod_classic_hle_np_notify_play_start(void)
{
    if (!np.enabled || np.armed) {
        return;
    }
    np.armed = true;
    np.attempts = 0;
    timer_mod(np.timer,
              qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + NP_FIRST_MS * 1000000LL);
    if (np.trace) {
        fprintf(stderr, "HLE_NP: play start seen, retry timer armed\n");
    }
}

/* --------------------------------------------------------------- init ------- */

static bool np_env_on(const char *name)
{
    const char *e = getenv(name);
    return e && *e && *e != '0';
}
static uint32_t np_env_hex(const char *name, uint32_t def)
{
    const char *e = getenv(name);
    return (e && *e) ? (uint32_t)strtoul(e, NULL, 0) : def;
}

void ipod_classic_hle_np_init(ARMCPU *cpu)
{
    if (!np_env_on("HLE_NP")) {
        return;   /* default build: install nothing, byte-identical baseline */
    }
    memset(&np, 0, sizeof(np));
    np.cpu = cpu;
    np.enabled = true;
    np.trace = np_env_on("HLE_NP_TRACE");
    np.dec_only = np_env_on("HLE_NP_DEC_ONLY");
    np.src_buf = np_env_hex("HLE_NP_SRC", NP_COPY_SRC_BUF);
    np.gate_byte = np_env_hex("HLE_NP_GATE", NP_COPY_GATE_BYTE);
    np.timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, np_attempt, &np);

    fprintf(stderr, "HLE_NP: armed (%s; decoder 0x%08x step 0x%08x; src 0x%08x "
            "gate 0x%08x)\n", np.dec_only ? "decoder-only" : "decode+bind+motor",
            NP_DECODER_OBJ, NP_DECODE_STEP, np.src_buf, np.gate_byte);
}
