/*
 * pctrace.c -- non-invasive guest-PC tracer for the iPod-Classic 6G retailOS.
 *
 * Purpose (wiki/19-classic6g-dsp-hle.md, wiki/15-muros-classic6g.md muro 12):
 * follow the audio-player start-up handler WITHOUT stopping the CPU. Attaching
 * gdb to trace the play handler desynchronises the click-wheel input latch
 * (the DOWN/UP packets get reordered while the CPU is halted at a breakpoint),
 * so a SELECT that would otherwise dispatch is lost. A TCG plugin observes the
 * guest from inside the translator and never halts it: the input path stays
 * bit-identical to a normal run.
 *
 * It watches a set of guest virtual PCs. Every time execution passes through
 * one it bumps a counter; a summary of all counts is printed at exit. To keep
 * hot functions (strcmp) from flooding the log, per-hit lines are only printed
 * inside a short wall-clock WINDOW that a "trigger" PC opens -- so the natural
 * trigger is the key dispatcher: press a key, and the plugin logs the handler
 * chain that the key set off, then goes quiet again.
 *
 * Guest VAs here are RUNTIME addresses: file_offset + 0x07FF51B8. They are what
 * the CPU actually executes, which is exactly what qemu_plugin_insn_vaddr()
 * returns, so no adjustment is needed inside the plugin.
 *
 * Build:   (from the QEMU build dir)
 *   make -C contrib/plugins pctrace        # or: cd build/contrib/plugins && make
 * Use:
 *   qemu-system-arm -M iPod-Classic ... \
 *     -plugin contrib/plugins/libpctrace.so,out=/home/you/6g/pc.log
 * Optional args (comma-separated after the .so path):
 *   out=PATH        write trace to PATH (default: stderr, captured like DISPTRACE)
 *   window=MS       ms a trigger keeps per-hit logging open (default 800)
 *   always=on       log every hit regardless of window (bounded by cap=)
 *   cap=N           max per-hit lines per PC in always mode (default 2000)
 *   pc=0xADDR:LABEL add a watched PC (repeatable) -- e.g. HandleSelect once known
 *   trig=0xADDR     also treat this PC as a window trigger (repeatable)
 *   quiet=0xADDR    make a watched PC count-only (no per-hit lines); strcmp is
 *                   count-only by default
 *   loud=0xADDR     undo quiet for a PC (emit per-hit lines again)
 *   nodefault=on    do not install the built-in PC table (only pc=/trig= args)
 *
 * License: GNU GPL, version 2 or later (see COPYING).
 */
#include <glib.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <qemu-plugin.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

typedef struct {
    uint64_t addr;
    const char *label;
    bool trigger;       /* opens the per-hit logging window */
    bool quiet;         /* count only, never emit a per-hit line (hot PCs) */
    guint64 count;      /* total times executed (all run) */
} WatchPC;

/* Built-in table. Addresses are VERIFIED runtime VAs from wiki/19 + task brief:
 * base = osos file offset + 0x07FF51B8.
 * columns: addr, label, trigger, quiet */
static const WatchPC default_pcs[] = {
    { 0x0803f390, "dispatch_key",               true,  false, 0 }, /* keycode router; SELECT=3 (TRIGGER) */
    { 0x0803f3e4, "pmu_hold_gate",              true,  false, 0 }, /* cmp r0,#0; popne if hold!=0 (TRIGGER) */
    { 0x08170e94, "Dhandler_router",            false, false, 0 }, /* D-handler / event router */
    { 0x08170da4, "queue_publish",              false, false, 0 }, /* publishes into the event queue */
    { 0x08105a1c, "dispatch_to_ui",             false, false, 0 }, /* dispatch to UI layer */
    { 0x081f87f8, "media_dispatch_chain",       false, false, 0 }, /* candidate media-dispatch string chain */
    { 0x081fa9e4, "HandleShuffleSongsSelected", false, false, 0 }, /* wall candidate: handler-only rule */
    { 0x08328090, "strcmp",                     false, true,  0 }, /* VERY hot: count only (see summary) */
};

static GArray *watch;               /* array of WatchPC */
static FILE *out;
static gint64 window_us;            /* per-hit logging window length (microseconds) */
static gint64 window_end;           /* monotonic-us deadline; 0 = closed */
static gint64 t0;                   /* first-hit timestamp, for relative stamps */
static bool always;                 /* log every hit, ignore window */
static guint64 cap;                 /* per-PC line cap in always mode */
static guint64 lines;               /* total lines emitted (global sanity cap) */

/*
 * RANGE TRACE (discovery mode). range=0xLO-0xHI logs the first-seen PC of every
 * translated block whose start PC falls in [LO,HI), but ONLY while a trigger
 * window is open, and each PC at most once per window opening (deduped by
 * win_gen). This reveals which functions a keypress sets off without halting the
 * CPU -- exactly what is needed to find the song-list HandleSelect, whose address
 * is not in any pointer table. Outside the window the callback is a single time
 * compare, so it does not perturb boot timing enough to trip the reset.
 */
typedef struct {
    uint64_t addr;
    guint last_gen;                 /* win_gen at which this PC was last logged */
    guint64 count;
} RangePC;
static uint64_t range_lo, range_hi; /* [lo,hi); hi==0 disables range trace */
static GHashTable *range_tbl;       /* addr -> RangePC* */
static guint win_gen;               /* bumped each time a trigger opens the window */
static const guint64 PCTRACE_LINE_MAX = 5000000;

/*
 * MEMORY-WRITE WATCH (the non-invasive watchpoint). memw=0xLO-0xHI logs every
 * guest-CPU store whose address falls in [LO,HI), with the store's PC and size.
 * This is the tool the brief asks for on the audio PCM ping-pong buffers
 * (IRAM 0x22008c5c / 0x22009c5c): whatever ARM code the MeCCA software decoder
 * runs to fill those buffers will show up here as the writing PC -- the Branch-A
 * capture -- without halting the CPU the way a gdb watchpoint does (which
 * desyncs the click-wheel latch). Always-on (not window-gated): the decode task
 * runs asynchronously, off the keypress window. Capped so a memcpy loop cannot
 * flood the log; the count keeps rising past the cap so the summary is honest.
 * QEMU 8.2's plugin API cannot read the stored value, so the value is confirmed
 * out-of-band (AUDIOWAV RMS, or an xp of the buffer once a writer PC is known).
 */
static uint64_t memw_lo, memw_hi;   /* [lo,hi); hi==0 disables mem watch */
static guint64 memw_cap = 400;      /* max logged store lines */
static guint64 memw_total;          /* total stores seen in range (uncapped) */
static guint64 memw_lines;          /* logged store lines so far */
static GHashTable *memw_pcs;        /* distinct writing PCs -> count */

/*
 * MEM-WRITE-AT-PC (surgical). memwpc=0xADDR:LABEL registers a store callback on
 * exactly the instruction at ADDR and logs the DESTINATION ADDRESS of each store
 * it makes. Unlike memw= (which filters a wide address range and needs a
 * callback on every instruction), this only fires at the one PC, so it is cheap
 * enough to leave on during sustained playback. It answers "where does this copy
 * write?" -- e.g. does the audio copy 0x08016eb4 target the DMA ping-pong, and
 * does its target move when the SM1 position advances? The stored VALUE is still
 * not readable under the 8.2 plugin API, but the address is what locates a
 * buffer. Capped per PC so a copy loop cannot flood the log.
 */
typedef struct {
    uint64_t pc;
    const char *label;
    guint64 count;
    guint64 logged;
} MemwPC;
static GArray *memwpc;              /* array of MemwPC */
static guint64 memwpc_cap = 40;     /* max logged lines per watched PC */

static WatchPC *find_pc(uint64_t addr)
{
    for (guint i = 0; i < watch->len; i++) {
        WatchPC *w = &g_array_index(watch, WatchPC, i);
        if (w->addr == addr) {
            return w;
        }
    }
    return NULL;
}

static void add_pc(uint64_t addr, const char *label, bool trigger, bool quiet)
{
    WatchPC *ex = find_pc(addr);
    if (ex) {                       /* merge: keep label, OR-in trigger */
        ex->trigger = ex->trigger || trigger;
        return;
    }
    WatchPC w = { addr, g_strdup(label), trigger, quiet, 0 };
    g_array_append_val(watch, w);
}

/* Executed on the vCPU thread every time a watched PC runs. Never halts. */
static void vcpu_pc_exec(unsigned int cpu_index, void *udata)
{
    WatchPC *w = (WatchPC *)udata;
    gint64 now = g_get_monotonic_time();

    if (t0 == 0) {
        t0 = now;
    }
    w->count++;

    if (w->trigger) {
        /* (re)open the logging window and announce the trigger itself */
        window_end = now + window_us;
        win_gen++;                  /* new window: range PCs may log again */
    }

    if (w->quiet) {                 /* count-only PC (e.g. strcmp): no line */
        return;
    }
    bool in_window = always || (window_end && now <= window_end);
    if (!in_window) {
        return;
    }
    if (lines >= PCTRACE_LINE_MAX) {
        return;
    }
    if (always && cap && w->count > cap) {
        return;
    }

    lines++;
    fprintf(out,
            "PCTRACE +%8" PRId64 "us cpu%u  %-28s @0x%08" PRIx64 "  #%" PRIu64
            "%s\n",
            now - t0, cpu_index, w->label, w->addr, w->count,
            w->trigger ? "  <TRIG>" : "");
    fflush(out);
}

/* Range-trace callback: log this TB's start PC once per open window. */
static void vcpu_range_exec(unsigned int cpu_index, void *udata)
{
    gint64 now = g_get_monotonic_time();
    if (!window_end || now > window_end) {
        return;                     /* window closed: cheapest possible path */
    }
    RangePC *r = (RangePC *)udata;
    r->count++;
    if (r->last_gen == win_gen) {
        return;                     /* already logged this window opening */
    }
    r->last_gen = win_gen;
    if (t0 == 0) {
        t0 = now;
    }
    if (lines >= PCTRACE_LINE_MAX) {
        return;
    }
    lines++;
    fprintf(out, "PCTRACE +%8" PRId64 "us cpu%u  RANGE                         "
            "@0x%08" PRIx64 "  #%" PRIu64 "\n",
            now - t0, cpu_index, r->addr, r->count);
    fflush(out);
}

/* Memory-store callback: log stores that land in the watched [lo,hi) range. */
static void vcpu_mem_w(unsigned int cpu_index, qemu_plugin_meminfo_t info,
                       uint64_t vaddr, void *udata)
{
    if (vaddr < memw_lo || vaddr >= memw_hi) {
        return;
    }
    if (!qemu_plugin_mem_is_store(info)) {
        return;
    }
    uint64_t pc = (uint64_t)(uintptr_t)udata;
    memw_total++;
    if (memw_pcs) {
        gpointer key = (gpointer)(uintptr_t)pc;
        guint64 c = (guint64)(uintptr_t)g_hash_table_lookup(memw_pcs, key);
        g_hash_table_insert(memw_pcs, key, (gpointer)(uintptr_t)(c + 1));
    }
    if (memw_lines >= memw_cap) {
        return;
    }
    memw_lines++;
    gint64 now = g_get_monotonic_time();
    if (t0 == 0) {
        t0 = now;
    }
    unsigned sh = qemu_plugin_mem_size_shift(info);
    fprintf(out, "PCTRACE +%8" PRId64 "us cpu%u  MEMW  pc=0x%08" PRIx64
            "  addr=0x%08" PRIx64 "  %ubytes  (#%" PRIu64 ")\n",
            now - t0, cpu_index, pc, vaddr, 1u << sh, memw_total);
    fflush(out);
}

/* Store callback registered only on a memwpc-watched PC: log the dest address. */
static void vcpu_mem_at_pc(unsigned int cpu_index, qemu_plugin_meminfo_t info,
                           uint64_t vaddr, void *udata)
{
    MemwPC *m = (MemwPC *)udata;

    if (!qemu_plugin_mem_is_store(info)) {
        return;
    }
    m->count++;
    if (m->logged >= memwpc_cap) {
        return;
    }
    m->logged++;
    gint64 now = g_get_monotonic_time();
    if (t0 == 0) {
        t0 = now;
    }
    unsigned sh = qemu_plugin_mem_size_shift(info);
    fprintf(out, "PCTRACE +%8" PRId64 "us cpu%u  MEMWPC %-14s pc=0x%08" PRIx64
            "  addr=0x%08" PRIx64 "  %ubytes  (#%" PRIu64 ")\n",
            now - t0, cpu_index, m->label, m->pc, vaddr, 1u << sh, m->count);
    fflush(out);
}

static MemwPC *find_memwpc(uint64_t pc)
{
    if (!memwpc) {
        return NULL;
    }
    for (guint i = 0; i < memwpc->len; i++) {
        MemwPC *m = &g_array_index(memwpc, MemwPC, i);
        if (m->pc == pc) {
            return m;
        }
    }
    return NULL;
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    size_t n = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        uint64_t va = qemu_plugin_insn_vaddr(insn);
        MemwPC *m = find_memwpc(va);
        if (m) {
            qemu_plugin_register_vcpu_mem_cb(
                insn, vcpu_mem_at_pc, QEMU_PLUGIN_CB_NO_REGS,
                QEMU_PLUGIN_MEM_W, m);
        }
        WatchPC *w = find_pc(va);
        if (w) {
            qemu_plugin_register_vcpu_insn_exec_cb(
                insn, vcpu_pc_exec, QEMU_PLUGIN_CB_NO_REGS, w);
        }
        /* Range trace only cares about the TB's first instruction. */
        if (i == 0 && range_hi && va >= range_lo && va < range_hi) {
            RangePC *r = g_hash_table_lookup(range_tbl, &va);
            if (!r) {
                r = g_new0(RangePC, 1);
                r->addr = va;
                uint64_t *key = g_new(uint64_t, 1);
                *key = va;
                g_hash_table_insert(range_tbl, key, r);
            }
            qemu_plugin_register_vcpu_insn_exec_cb(
                insn, vcpu_range_exec, QEMU_PLUGIN_CB_NO_REGS, r);
        }
        /* Memory-write watch: every instruction may store; the callback
         * filters by address, so register on all of them when armed. */
        if (memw_hi) {
            qemu_plugin_register_vcpu_mem_cb(
                insn, vcpu_mem_w, QEMU_PLUGIN_CB_NO_REGS,
                QEMU_PLUGIN_MEM_W, (void *)(uintptr_t)va);
        }
    }
}

static void plugin_exit(qemu_plugin_id_t id, void *p)
{
    fprintf(out, "\n=== PCTRACE summary (hit counts, run to run) ===\n");
    for (guint i = 0; i < watch->len; i++) {
        WatchPC *w = &g_array_index(watch, WatchPC, i);
        fprintf(out, "  %-28s @0x%08" PRIx64 "  hits=%" PRIu64 "%s\n",
                w->label, w->addr, w->count, w->trigger ? "  (trigger)" : "");
    }
    if (memw_hi) {
        fprintf(out, "  --- mem-write watch 0x%08" PRIx64 "-0x%08" PRIx64
                ": %" PRIu64 " stores from %u distinct PC(s) ---\n",
                memw_lo, memw_hi, memw_total,
                memw_pcs ? g_hash_table_size(memw_pcs) : 0);
        if (memw_pcs) {
            GHashTableIter it;
            gpointer k, v;
            g_hash_table_iter_init(&it, memw_pcs);
            while (g_hash_table_iter_next(&it, &k, &v)) {
                fprintf(out, "      writer pc=0x%08" PRIxPTR "  stores=%"
                        PRIuPTR "\n", (uintptr_t)k, (uintptr_t)v);
            }
        }
    }
    if (memwpc) {
        fprintf(out, "  --- mem-write-at-pc watches ---\n");
        for (guint i = 0; i < memwpc->len; i++) {
            MemwPC *m = &g_array_index(memwpc, MemwPC, i);
            fprintf(out, "      %-14s pc=0x%08" PRIx64 "  stores=%" PRIu64 "\n",
                    m->label, m->pc, m->count);
        }
    }
    fflush(out);
    if (out != stderr) {
        fclose(out);
    }
}

/* parse "0xADDR:LABEL" or "0xADDR" */
static bool parse_pc_arg(const char *s, uint64_t *addr, char **label)
{
    char *colon = strchr(s, ':');
    char *end = NULL;
    *addr = g_ascii_strtoull(s, &end, 16);
    if (end == s) {
        return false;
    }
    *label = colon ? g_strdup(colon + 1) : g_strdup_printf("0x%08" PRIx64, *addr);
    return true;
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info, int argc,
                                           char **argv)
{
    out = stderr;
    window_us = 800 * 1000;         /* 800 ms */
    cap = 2000;
    bool nodefault = false;

    /* First pass: options that must be known before we build the table. */
    for (int i = 0; i < argc; i++) {
        g_auto(GStrv) kv = g_strsplit(argv[i], "=", 2);
        if (!g_strcmp0(kv[0], "nodefault")) {
            nodefault = kv[1] && !g_strcmp0(kv[1], "on");
        } else if (!g_strcmp0(kv[0], "window")) {
            window_us = g_ascii_strtoull(kv[1], NULL, 10) * 1000;
        } else if (!g_strcmp0(kv[0], "always")) {
            always = kv[1] && !g_strcmp0(kv[1], "on");
        } else if (!g_strcmp0(kv[0], "cap")) {
            cap = g_ascii_strtoull(kv[1], NULL, 10);
        } else if (!g_strcmp0(kv[0], "range") && kv[1]) {
            /* range=0xLO-0xHI */
            g_auto(GStrv) lh = g_strsplit(kv[1], "-", 2);
            if (lh[0] && lh[1]) {
                range_lo = g_ascii_strtoull(lh[0], NULL, 16);
                range_hi = g_ascii_strtoull(lh[1], NULL, 16);
            }
        } else if (!g_strcmp0(kv[0], "memw") && kv[1]) {
            /* memw=0xLO-0xHI : log guest-CPU stores in [lo,hi) */
            g_auto(GStrv) lh = g_strsplit(kv[1], "-", 2);
            if (lh[0] && lh[1]) {
                memw_lo = g_ascii_strtoull(lh[0], NULL, 16);
                memw_hi = g_ascii_strtoull(lh[1], NULL, 16);
            }
        } else if (!g_strcmp0(kv[0], "memcap") && kv[1]) {
            memw_cap = g_ascii_strtoull(kv[1], NULL, 10);
        } else if (!g_strcmp0(kv[0], "memwpccap") && kv[1]) {
            memwpc_cap = g_ascii_strtoull(kv[1], NULL, 10);
        } else if (!g_strcmp0(kv[0], "memwpc") && kv[1]) {
            /* memwpc=0xADDR:LABEL : log store dest addresses at this PC */
            uint64_t a; char *l;
            if (parse_pc_arg(kv[1], &a, &l)) {
                if (!memwpc) {
                    memwpc = g_array_new(false, false, sizeof(MemwPC));
                }
                MemwPC m = { a, l, 0, 0 };
                g_array_append_val(memwpc, m);
            }
        } else if (!g_strcmp0(kv[0], "out")) {
            FILE *f = fopen(kv[1], "w");
            if (!f) {
                fprintf(stderr, "pctrace: cannot open out=%s\n", kv[1]);
                return -1;
            }
            out = f;
        }
    }

    range_tbl = g_hash_table_new(g_int64_hash, g_int64_equal);
    memw_pcs = g_hash_table_new(g_direct_hash, g_direct_equal);

    watch = g_array_new(false, false, sizeof(WatchPC));

    if (!nodefault) {
        for (size_t i = 0; i < G_N_ELEMENTS(default_pcs); i++) {
            add_pc(default_pcs[i].addr, default_pcs[i].label,
                   default_pcs[i].trigger, default_pcs[i].quiet);
        }
    }

    /* Second pass: pc= / trig= / quiet= / loud= additions. */
    for (int i = 0; i < argc; i++) {
        g_auto(GStrv) kv = g_strsplit(argv[i], "=", 2);
        if (!g_strcmp0(kv[0], "pc") && kv[1]) {
            uint64_t a; char *l;
            if (parse_pc_arg(kv[1], &a, &l)) {
                add_pc(a, l, false, false);
                g_free(l);
            }
        } else if (!g_strcmp0(kv[0], "trig") && kv[1]) {
            uint64_t a = g_ascii_strtoull(kv[1], NULL, 16);
            add_pc(a, NULL, true, false);  /* label kept if already present */
            WatchPC *w = find_pc(a);
            if (w && !w->label) {
                w->label = g_strdup_printf("0x%08" PRIx64, a);
            }
        } else if (!g_strcmp0(kv[0], "quiet") && kv[1]) {
            WatchPC *w = find_pc(g_ascii_strtoull(kv[1], NULL, 16));
            if (w) {
                w->quiet = true;
            }
        } else if (!g_strcmp0(kv[0], "loud") && kv[1]) {
            WatchPC *w = find_pc(g_ascii_strtoull(kv[1], NULL, 16));
            if (w) {
                w->quiet = false;
            }
        }
    }

    if (watch->len == 0) {
        fprintf(stderr, "pctrace: no PCs to watch (use pc=... or drop "
                        "nodefault)\n");
        return -1;
    }

    fprintf(out, "=== PCTRACE armed: %u PC(s), window=%" PRId64 "ms, %s ===\n",
            watch->len, window_us / 1000,
            always ? "always-log" : "window-gated");
    if (range_hi) {
        fprintf(out, "    range trace 0x%08" PRIx64 "-0x%08" PRIx64
                " (first-TB-PC, deduped per window)\n", range_lo, range_hi);
    }
    if (memw_hi) {
        fprintf(out, "    mem-write watch 0x%08" PRIx64 "-0x%08" PRIx64
                " (store PC+addr, always-on, cap=%" PRIu64 ")\n",
                memw_lo, memw_hi, memw_cap);
    }
    for (guint i = 0; i < watch->len; i++) {
        WatchPC *w = &g_array_index(watch, WatchPC, i);
        fprintf(out, "    watch %-28s @0x%08" PRIx64 "%s%s\n",
                w->label, w->addr, w->trigger ? "  <TRIG>" : "",
                w->quiet ? "  (count-only)" : "");
    }
    fflush(out);

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
