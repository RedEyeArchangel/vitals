#pragma once
/* Neutral IPC contract between `vitals` and `ram_oc_daemon`. Owned by
 * vitals; contains no vendor code and is included by both sides — the
 * daemon (which links the GPLv3 ram_oc/ sources) and vitals (which must
 * not). Pure POD: fixed-width primitives and fixed-size arrays only, so the
 * layout is stable across the shared-memory boundary.
 *
 * Field names mirror the physical properties they represent (vcore, tCL,
 * ...) for readability — that's not copyrightable expression, just labels —
 * but this struct, the daemon that fills it, and the client that reads it
 * are all original to vitals.
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bump VITALS_IPC_VERSION whenever VitalsTelemetryFrame's layout changes.
 * The version is baked into the shm/socket names so a rebuilt vitals never
 * mmaps its (possibly larger/reshaped) frame struct over a still-running
 * old-build daemon's differently-sized segment — no stale-daemon SIGBUS
 * risk, and no need to find/kill the old daemon (which may be root-owned
 * and unkillable by an unprivileged vitals) before the new build works.
 * The orphaned old daemon just keeps running harmlessly under its own old
 * name until it's killed or the system reboots. */
#define VITALS_IPC_VERSION 2
#define VITALS_IPC_STRINGIFY_(x) #x
#define VITALS_IPC_STRINGIFY(x) VITALS_IPC_STRINGIFY_(x) /* forces x to expand first */
#define VITALS_IPC_VERSION_STR VITALS_IPC_STRINGIFY(VITALS_IPC_VERSION)

#define VITALS_SHM_NAME     "/vitals_ram_oc_v" VITALS_IPC_VERSION_STR
#define VITALS_SOCKET_RUN_PATH "/run/vitals_ram_oc_v" VITALS_IPC_VERSION_STR ".sock"
#define VITALS_SOCKET_TMP_PATH "/tmp/vitals_ram_oc_v" VITALS_IPC_VERSION_STR ".sock"

#define MAX_DIMM_MODULES 8
#define MAX_CPU_CORES    64
#define MAX_FANS         8

typedef struct {
    char manufacturer[64];
    char part_number[64];
    char serial_number[64];
    char capacity_display[32];
    char slot_display[64];
    char rank[4]; /* "SR" / "DR" / "QR" */
} VitalsDramModulePOD;

typedef struct {
    char label[16];
    int32_t rpm;
} VitalsFanPOD;

/* Written only by ram_oc_daemon's single background thread; read by any
 * number of vitals processes via the Seqlock helpers below (PROT_READ-only
 * mapping on the reader side — see vitals_shm_try_read). */
typedef struct {
    volatile uint32_t sequence_id; /* even = stable frame; odd = write in progress */
    bool supported;      /* generic telemetry available (DIMM/SPD, memory speed,
                           * per-core temp/usage/freq, package power) — true on
                           * any Linux box with a live daemon */
    bool smu_supported;  /* AMD ryzen_smu also available: voltages, PPT/clocks
                           * below, and the DRAM sub-timings, are real */

    char cpu_codename[64];      /* empty unless smu_supported */
    char cpu_model[128];        /* real CPU model name, any vendor */
    char smu_version[64];
    char pm_table_version[64];
    char board_display_line[256];

    float memory_frequency;
    char memory_type[16]; /* "DDR5" etc, precomputed by the daemon */
    float package_power_w; /* AMD: PM table PPT. Intel/other: RAPL. 0 if neither. */

    /* SMU */
    float ppt_w, vcore, vsoc, vddp, vddg_ccd, vddg_iod, vdd_misc;
    float cpu_vddio, mem_vdd, mem_vddq, mem_vpp, vid;
    float bclk_mhz, fclk_mhz, uclk_mhz, mclk_mhz;

    bool has_tdie;        float tdie_c;
    bool has_tctl;        float tctl_c;
    bool has_tccd1;       float tccd1_c;
    bool has_tccd2;       float tccd2_c;
    bool has_iod_hotspot; float iod_hotspot_c;

    uint32_t core_voltage_count;              float core_voltages[MAX_CPU_CORES];
    uint32_t core_temp_count;                 float core_temps_c[MAX_CPU_CORES];
    uint32_t core_usage_count;                float core_usage_pct[MAX_CPU_CORES];
    uint32_t core_freq_count;                 float core_freq_mhz[MAX_CPU_CORES];
    uint32_t spd_temp_count;                  float spd_temps_c[MAX_DIMM_MODULES];

    uint32_t module_count;
    VitalsDramModulePOD modules[MAX_DIMM_MODULES];

    uint32_t fan_count;
    VitalsFanPOD fans[MAX_FANS];

    /* DRAM timings */
    uint32_t tcl, trcd_rd, trcd_wr, trp, tras, trc;
    uint32_t trrds, trrdl, tfaw, twr, tcwl;
    uint32_t rtp, wtrs, wtrl, rdwr, wrrd;
    uint32_t rdrd_scl, wrwr_scl;
    uint32_t rdrd_sc, rdrd_sd, rdrd_dd;
    uint32_t wrwr_sc, wrwr_sd, wrwr_dd;
    uint32_t refi, wrpre, rdpre;
    uint32_t trc_page, mod, mod_pda, mrd, mrd_pda;
    uint32_t stag, stag_sb, cke, xp;
    uint32_t phy_wrd, phy_wrl, phy_rdl;
    uint32_t rfc, rfc2, rfcsb;
    float trefi_ns, trfc_ns;
    bool gdm_enabled, power_down_enabled;
    char cmd2t[8];
} VitalsTelemetryFrame;

/* --- Seqlock helpers: shared by the daemon (single writer) and vitals
 * (any number of readers). Header-only so both sides — one compiled as C,
 * one as C++ — use the exact same synchronization, not two hand-rolled
 * copies that could drift out of sync. --- */

static inline void vitals_shm_begin_write(volatile VitalsTelemetryFrame* f) {
    __atomic_store_n(&f->sequence_id, f->sequence_id + 1, __ATOMIC_RELAXED);
    /* A release *store* doesn't stop the data writes that follow from being
     * hoisted above it; the fence does (standard seqlock writer). */
    __atomic_thread_fence(__ATOMIC_RELEASE);
}

static inline void vitals_shm_end_write(volatile VitalsTelemetryFrame* f) {
    __atomic_store_n(&f->sequence_id, f->sequence_id + 1, __ATOMIC_RELEASE);
}

/* Lock-free read: retries if a write was in progress or landed mid-copy.
 * Returns true and fills *dst with a consistent snapshot; false means the
 * caller should retry (a writer was mid-update) — callers should cap
 * retries rather than spin forever, though in practice a single retry
 * almost always succeeds since writes are infrequent (~1/s) and fast. */
static inline bool vitals_shm_try_read(const volatile VitalsTelemetryFrame* src, VitalsTelemetryFrame* dst) {
    uint32_t seq1 = __atomic_load_n(&src->sequence_id, __ATOMIC_ACQUIRE);
    if (seq1 & 1u) return false; /* write in progress */
    memcpy(dst, (const void*)src, sizeof(VitalsTelemetryFrame));
    /* Keeps the memcpy's loads from sinking below the re-check. */
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    uint32_t seq2 = __atomic_load_n(&src->sequence_id, __ATOMIC_RELAXED);
    return seq1 == seq2;
}

typedef enum {
    CMD_PING = 1,
    CMD_RUN_BENCHMARK = 2
} VitalsIpcCommand;

/* Socket request: a single int32_t command code, sent as raw bytes. */

typedef struct {
    float lat_l1_ns;
    float lat_l2_ns;
    float lat_l3_ns;
    float lat_dram_ns;
    uint32_t bw_read_mbs;
    uint32_t bw_write_mbs;
    uint32_t bw_copy_mbs;
} VitalsBenchResultPOD;

/* CMD_PING response: a single byte, 1 = ok. */
/* CMD_RUN_BENCHMARK response: one VitalsBenchResultPOD, sent once bench_run()
 * completes (the daemon runs it on a worker thread so its ~1s telemetry
 * refresh loop keeps running concurrently — see ram_oc_helper/daemon_main.c). */

#ifdef __cplusplus
}
#endif
