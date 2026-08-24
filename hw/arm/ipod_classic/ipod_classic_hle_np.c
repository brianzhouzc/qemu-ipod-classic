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
#include "hw/arm/ipod_classic/ipod_classic_hle_audio.h"

/* --------------- VERIFIED guest addresses (osos VA = off + 0x07FF51B8) ------ */

#define NP_DECODER_OBJ      0x0ba9a8a4u
#define NP_DECODER_VTABLE   0x088d1948u
#define NP_DEC_CTX_OFF      0x34u   /* [obj+0x34] = decode ctx                  */
#define NP_DEC_OBJ56_OFF    0x38u   /* decode gate: must be 0 (0x81232d8)       */
#define NP_DEC_OBJ132_OFF   0x84u   /* decode gate: must be 0 (0x81232d8)       */
#define NP_DEC_CTX_GATE_OFF 0x60u   /* gate[ctx+0x60] != 0  <=>  primed (0x4)   */
#define NP_DEC_CTX_READER   0x48u   /* [ctx+0x48] = reader                      */
#define NP_DECODE_STEP      0x081232d8u  /* decode-a-frame case (obj, 0)         */

/* Engine copy binding (measured live at the bring-up render, wiki/19 11.20).
 * copy 0x08016e44: src = [r5+12] + (([r4+4]-r6-[r5])<<2); gate [r7+5]==1 (when
 * [r4]==0). Captured: r5=0x088890c0 [r5+12]=0x08889098 [r5]=0; r4=0x0bb12d54
 * [r4]=0; r7=0x0bb12eb4 [r7+5]=0. So the engine reads PCM from a ring based at
 * 0x08889098; we keep that ring filled with the real (host-decoded) song so the
 * engine's OWN copy renders it (copy32>0) and Now Playing advances. */
#define NP_COPY_SRC_DESCR   0x088890c0u  /* r5; source base at [descr+12]        */
#define NP_COPY_SRC_OFF     12u
#define NP_COPY_SRC_BUF     0x08889098u  /* = [0x088890cc]; ring base the copy reads */
#define NP_COPY_GATE_BYTE   0x0bb12eb9u  /* [r7+5]; must be 1 to copy            */
#define NP_COPY_R4          0x0bb12d54u  /* r4 descriptor ([r4]=type,[r4+4]=rdpos)  */
#define NP_SRC_WINDOW       0x8000u      /* bytes of PCM we keep live at the ring   */

/* Render doorbell (fast-sema t25 recv()s on) and engine object. */
#define NP_RING_OBJ         0x220106d0u
#define NP_RING_WAITERS_OFF 0x04u
#define NP_ENGOBJ           0x220106dcu
/*
 * [engobj+0x44]: the render pump's abort flag. Nonzero makes 0x22006118 return
 * -1 at 0x22006170 before touching anything, which stops the motor re-posting
 * a job and parks the whole render loop. The guest's own doorbell (0x2200683c)
 * clears it.
 */
#define NP_ENG_ABORT_OFF    0x44
/* The motor's job counter: 0 (or negative) means "nothing to render". */
#define NP_JOBCOUNT         0x22008c1cu
#define NP_ENG_GATE_OFF     71u
#define NP_ENG_TYPE_OFF     88u   /* [engobj+88] render type (0xff = unconfigured) */
#define NP_SIGNAL_STUB      0x22006870u  /* r0=ring;r1=0;b V()  (pure doorbell)  */

/* Guest engine render 0x0808170c: fetches engobj itself (getter 0x8015860),
 * bails if [engobj+71]==0, else renders the current stream via mixer 0x0809a74c
 * -> render 0x08016f68 -> copy 0x08016e44. No KS-wait, so callable from idle.
 * Calling it SYNCHRONOUSLY right after we set the stream source + copy gate wins
 * the timing race that the 5 ms idle poke lost (no task interleaves the copy). */
#define NP_ENGINE_RENDER    0x0808170cu

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
    uint32_t   src_buf;       /* copy source ring base (HLE_NP_SRC or default)    */
    uint32_t   gate_byte;     /* copy gate byte addr (HLE_NP_GATE or default)    */
    uint32_t   r4;            /* copy r4 descriptor addr                          */
    size_t     pcm_cursor;    /* bytes of the host-decoded song fed into the ring */
    uint32_t   last_rdpos;    /* previous copy read cursor (words) for advance    */
    bool       feed_ready;    /* datapath decode available                        */
    bool       nofill;        /* HLE_NP_NOFILL: trace descriptors, don't write PCM */
    bool       type1;         /* HLE_NP_TYPE1: force engobj render type=1          */
    bool       call_engine;   /* HLE_NP_ENGINE: call engine directly vs ring bell  */
    bool       passive;       /* HLE_NP_PASSIVE: memory-only prefill, no PC hijack */
    bool       job;           /* HLE_NP_JOB: keep jobcount primed + abort flag clear */
    int        prefills;      /* passive prefill ticks applied                     */
    uint8_t    win[NP_SRC_WINDOW];
    int        attempts;
    int        kicks;
    int        dec_kicks;
    int        bind_kicks;
    int        feeds;
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
    /* Safe kick points where no live task's context is on the CPU: the RTXC idle
     * loop (WFI), a halted CPU, or the SUSTAIN reset-spin (0x08322b1c..30, the
     * nop spin after the suppressed deadlock-watchdog reset -- no task runs
     * there either). */
    bool in_idle = (pc >= NP_IDLE_LO && pc < NP_IDLE_HI);
    bool in_spin = (pc >= 0x08322b1cu && pc < 0x08322b34u);
    bool idle = cs->halted || in_idle || in_spin;
    uint32_t ret, ctx, waiter;

    np.pending = false;
    if (np.kicks >= NP_MAX_KICKS) {
        return;
    }
    if (!idle) {
        if (np.trace && np.deferred < 8) {
            fprintf(stderr, "HLE_NP: deferred (cpu busy) pc=0x%08x halted=%d\n",
                    pc, cs->halted);
        }
        np.deferred++;
        return;
    }
    /* Return to the idle loop after the synchronous call (never back into the
     * spin, which would just re-spin). */
    ret = in_idle ? pc : NP_IDLE_ENTRY;

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
     * Full mode: FEED the guest engine's own copy source with the real
     * (host-decoded) song, open the copy gate, and ring the render doorbell so
     * the guest's OWN engine renders (copy 0x08016eb4 > 0) and Now Playing
     * advances. PCM is the real song (shared with the data-path decode); only
     * the control bindings (ring fill + gate) are set here.
     */
    if (!np.feed_ready) {
        return;   /* nothing to render yet (song not decoded) */
    }

    /* 1. Fill EXACTLY the bytes the copy reads (measured: copy src = [r5+12] +
     *    [r4+4]<<2 = 0x08889098 + 0, count = [r4+16] bytes). The buffer is a tiny
     *    staging area whose descriptor r5 sits right after it (0x088890c0 =
     *    buf+0x28), so we must fill only `count` bytes -- a bigger window clobbers
     *    the descriptor. r4/r7 are the stable heap descriptors on the engine's
     *    DEFAULT render path (0x0bb12d54/0x0bb12eb4); we poke those. */
    if (!np.nofill) {
        uint32_t buf = np_rd32(NP_COPY_SRC_DESCR + NP_COPY_SRC_OFF);
        uint32_t off = np_rd32(np.r4 + 4);          /* [r4+4], read offset (words) */
        uint32_t cnt = np_rd32(np.r4 + 16) & 0xffff;/* [r4+16], byte count         */
        if (buf >= 0x08000000u && buf < 0x0c000000u && cnt > 0 && cnt <= 0x28) {
            uint32_t dst = buf + (off << 2);
            size_t got = ipod_classic_hle_audio_copy_pcm(np.pcm_cursor, np.win,
                                                         cnt);
            if (got > 0) {
                cpu_physical_memory_write(dst, np.win, got);
                np.pcm_cursor += got;
                if (np.pcm_cursor >= ipod_classic_hle_audio_pcm_len()) {
                    np.pcm_cursor = 0;
                }
                np.feeds++;
            }
        }
    }

    /* 2. Open the copy gate on the DEFAULT-path heap descriptors: [r7+5]=1 and
     *    [r4]==0 (so the copy takes the [r7+5] gate path), plus arm the engine
     *    render gate [engobj+71]=1. */
    np_wr8(np.gate_byte, 1);
    np_wr8(np.r4, 0);
    np_wr8(NP_ENGOBJ + NP_ENG_GATE_OFF, 1);
    if (np.type1) {
        np_wr8(NP_ENGOBJ + NP_ENG_TYPE_OFF, 1);
        np_wr8(NP_ENGOBJ + 70, 1);
    }

    /* 3. Trigger the render. Two modes:
     *    - default: RING THE DOORBELL (guest stub 0x22006870) so the NATURAL t25
     *      render path runs -- it uses the stable HEAP descriptor (0x0bb12d54/eb4)
     *      we poked in step 2, so the copy sees our gate+PCM.
     *    - HLE_NP_ENGINE: call the engine render directly (uses transient STACK
     *      descriptors; kept for experiments). */
    if (np.call_engine) {
        env->regs[0]  = 0;
        env->regs[1]  = 0;
        env->regs[14] = ret;
        env->thumb    = false;
        env->regs[15] = NP_ENGINE_RENDER;
    } else {
        if (waiter == 0) {
            return;   /* t25 not parked on the doorbell yet */
        }
        env->regs[0]  = NP_RING_OBJ;
        env->regs[1]  = 0;
        env->regs[14] = ret;
        env->thumb    = false;
        env->regs[15] = NP_SIGNAL_STUB;
    }
    cs->halted    = 0;
    np.bind_kicks++;
    np.kicks++;
    /* (cursor advanced in the fill step, by the bytes actually staged) */
    if (np.trace && np.bind_kicks <= 8) {
        uint32_t d = NP_COPY_SRC_DESCR;
        uint32_t buf = np_rd32(d + 12);
        fprintf(stderr, "HLE_NP: #%d cur=%zu stream[%08x] +0=%08x +4=%08x "
                "+8=%08x +c(buf)=%08x +10=%08x +14=%08x | buf->%08x %08x %08x %08x\n",
                np.bind_kicks, np.pcm_cursor, d, np_rd32(d), np_rd32(d+4),
                np_rd32(d+8), buf, np_rd32(d+16), np_rd32(d+20),
                np_rd32(buf), np_rd32(buf+4), np_rd32(buf+8), np_rd32(buf+12));
    }
    (void)ctx; (void)waiter;
}

/* ------------------------------------------- passive prefill (vCPU thread) -- */
/*
 * The idle-gated CPU hijack (np_do_kick) can never land during the deadlock:
 * the guest busy-loops in MeCCA spinlocks and never reaches the RTXC idle loop,
 * so every kick defers forever (measured: deferred climbs into the thousands,
 * kicks=0; wiki/19 §11.22-D). But the copy routine 0x08016e44 only needs three
 * MEMORY conditions to run its loop on the motor's single natural render:
 * [r4]==0, [r7+5]==1, and the source ring filled. Those are plain stores; they
 * do NOT need the CPU at idle. This callback keeps them primed on every tick so
 * that whenever the guest's OWN natural render runs, its copy sees real PCM and
 * copy32 fires -- no PC hijack, no injection window needed. It runs on the vCPU
 * thread (via async_run_on_cpu) so guest memory is coherent at a TB boundary.
 */
static void np_do_prefill(CPUState *cs, run_on_cpu_data data)
{
    np.pending = false;

    /*
     * 1. Ensure the source descriptor points at a valid staging buffer. In cold
     *    start [descr+12] reads 0 (the buffer is populated only by the render
     *    that is itself deadlocked, wiki/19 §11.18-C), so the copy would read
     *    from address 0. Force the descriptor to our known staging buffer
     *    (np.src_buf) whenever it is unset, and fill that buffer with the
     *    host-decoded song (or silence until the decode is ready, so the copy
     *    can still run and copy32 can fire on the motor's single natural render).
     */
    uint32_t buf = np_rd32(NP_COPY_SRC_DESCR + NP_COPY_SRC_OFF);
    if (buf < 0x08000000u || buf >= 0x0c000000u) {
        buf = np.src_buf;
        np_wr32(NP_COPY_SRC_DESCR + NP_COPY_SRC_OFF, buf);  /* [descr+12]=src */
    }
    if (!np.nofill && buf >= 0x08000000u && buf < 0x0c000000u) {
        uint32_t off = np_rd32(np.r4 + 4);
        uint32_t cnt = np_rd32(np.r4 + 16) & 0xffff;
        if (cnt == 0 || cnt > 0x28) {
            cnt = 0x28;                    /* default staging size (40 B) */
        }
        uint32_t dst = buf + (off << 2);
        size_t got = 0;
        if (np.feed_ready) {
            got = ipod_classic_hle_audio_copy_pcm(np.pcm_cursor, np.win, cnt);
        }
        if (got == 0) {
            memset(np.win, 0, cnt);        /* silence until decode is ready */
            got = cnt;
        }
        cpu_physical_memory_write(dst, np.win, got);
        np.feeds++;
    }

    /* 2. Keep the copy gate open on the stable heap descriptors and arm the
     *    engine render gates. Re-applied every tick so a guest clear cannot win
     *    for long. */
    np_wr8(np.gate_byte, 1);          /* [r7+5] = 1  */
    np_wr8(np.r4, 0);                 /* [r4]   = 0  */
    np_wr8(NP_ENGOBJ + NP_ENG_GATE_OFF, 1);
    np_wr8(NP_ENGOBJ + 70, 1);        /* engobj+0x46 render doorbell enable */
    if (np.type1) {
        np_wr8(NP_ENGOBJ + NP_ENG_TYPE_OFF, 1);
    }

    /*
     * 3. The job counter and the pump's abort flag (HLE_NP_JOB).
     *
     * Decoded this session: the motor thread ("MeCCADecode", IRAM 0x22002f5c)
     * does NOT act on the condvar notification it wakes from -- it re-tests the
     * integer job counter at 0x22008c1c and goes straight back to its 3000 ms
     * wait while that is <= 0 (0x22002fe0: ldr/cmp/ble). Only with a job does it
     * call the render pump (0x22005dec -> 0x22006118), and it DECREMENTS the
     * counter each pass. The loop is self-sustaining by design: when the pump
     * returns 0 the motor immediately re-posts a job (0x22005e38 ->
     * 0x220067e4, counter++), so the count holds steady and the render free-runs.
     *
     * What breaks it: the pump aborts with -1 before doing anything when
     * [engobj+0x44] != 0 (0x22006170, writing status 5 to 0x22008c54). One -1
     * and the motor skips the re-post, the counter decays to 0, and the loop
     * parks for good -- which is exactly the "structural deadlock" of the last
     * thirteen assaults. So: keep the abort flag clear and the counter primed,
     * and one successful pump should make the native render self-sustaining.
     */
    if (np.job) {
        np_wr8(NP_ENGOBJ + NP_ENG_ABORT_OFF, 0);   /* [engobj+0x44] = 0 */
        if ((int32_t)np_rd32(NP_JOBCOUNT) <= 0) {
            np_wr32(NP_JOBCOUNT, 1);
        }
    }
    np.prefills++;
    if (np.trace && np.prefills <= 6) {
        fprintf(stderr, "HLE_NP: PREFILL #%d gate[%08x]=1 r4[%08x]=0 "
                "srcbuf=%08x feed=%d head=%08x\n", np.prefills, np.gate_byte,
                np.r4, buf, np.feed_ready, np_rd32(buf));
    }
}

/* ---------------------------------------------------- the retry (iothread) -- */

static void np_attempt(void *opaque)
{
    if (np.kicks >= NP_MAX_KICKS) {
        np.armed = false;
        return;
    }
    np.feed_ready = ipod_classic_hle_audio_have_pcm();
    if (np.passive) {
        /* Memory-only prefill: no idle requirement, no PC hijack, and NOT gated
         * on feed_ready -- the host decode only reports ready ~60s after the
         * motor's single natural render (measured), so we must have the gate +
         * source primed from play-start. The prefill writes silence until real
         * PCM is available, so the copy can run regardless. */
        if (!np.pending) {
            np.pending = true;
            async_run_on_cpu(CPU(np.cpu), np_do_prefill, RUN_ON_CPU_NULL);
        }
    } else if (!np.pending &&
               (np.feed_ready || np_decoder_ctx() || np_ring_waiter())) {
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
    np.nofill = np_env_on("HLE_NP_NOFILL");
    np.type1 = np_env_on("HLE_NP_TYPE1");
    np.call_engine = np_env_on("HLE_NP_ENGINE");
    np.passive = np_env_on("HLE_NP_PASSIVE");
    np.job = np_env_on("HLE_NP_JOB");
    np.src_buf = np_env_hex("HLE_NP_SRC", NP_COPY_SRC_BUF);
    np.gate_byte = np_env_hex("HLE_NP_GATE", NP_COPY_GATE_BYTE);
    np.r4 = np_env_hex("HLE_NP_R4", NP_COPY_R4);
    np.timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, np_attempt, &np);

    /* We drive the guest engine directly; the data-path DMA feed must stand
     * down so the song is not double-fed (it shares its decoded PCM with us). */
    if (!np.dec_only) {
        ipod_classic_hle_audio_set_np_feed(true);
    }

    fprintf(stderr, "HLE_NP: armed (%s; decoder 0x%08x step 0x%08x; src 0x%08x "
            "gate 0x%08x r4 0x%08x)\n",
            np.dec_only ? "decoder-only" : "feed-engine-source",
            NP_DECODER_OBJ, NP_DECODE_STEP, np.src_buf, np.gate_byte, np.r4);
}
