#pragma once
/* Neutral IPC contract between `vitals` and `gpu_ctl_daemon`. Owned by
 * vitals; contains no vendor code and is included by both sides — the
 * daemon (root, does the actual sysfs/NVML/NvAPI writes) and vitals (which
 * only ever reads GpuCtlCapsPOD/GpuCtlConfigPOD and sends commands). Pure
 * POD: fixed-width primitives and fixed-size arrays only, so the layout is
 * stable across the socket boundary — same discipline as ram_oc_shm.h, see
 * that file for the sibling telemetry-daemon's version of this contract.
 *
 * Unlike ram_oc_daemon, this daemon needs real root unconditionally (AMD's
 * power1_cap/pwm1/pp_od_clk_voltage and NVML's Set* calls are root-only) and
 * is never spawned or elevated by vitals itself — it runs as a systemd
 * service the user installs once (see scripts/install-daemons.sh). There is no
 * shared-memory telemetry frame here: vitals already reads GPU telemetry
 * itself (src/metrics/gpu_sysfs.cpp); this socket is control-only.
 *
 * Field names mirror LACT's lact-schema/src/config.rs GpuConfig shape
 * (field-by-field, flattened into PODs) purely for readability when porting
 * settings — that's not copyrightable expression, just labels — but this
 * struct, the daemon that fills/applies it, and the client that reads/sends
 * it are all original to vitals.
 */
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bump VITALS_GPUCTL_IPC_VERSION whenever any POD below changes shape — see
 * ram_oc_shm.h's VITALS_IPC_VERSION comment for why this must be baked into
 * the socket name rather than relying on the daemon to notice a rebuild. */
#define VITALS_GPUCTL_IPC_VERSION 2
#define VITALS_GPUCTL_STRINGIFY_(x) #x
#define VITALS_GPUCTL_STRINGIFY(x) VITALS_GPUCTL_STRINGIFY_(x)
#define VITALS_GPUCTL_IPC_VERSION_STR VITALS_GPUCTL_STRINGIFY(VITALS_GPUCTL_IPC_VERSION)

#define VITALS_GPUCTL_SOCKET_PATH "/run/vitals_gpud_v" VITALS_GPUCTL_IPC_VERSION_STR ".sock"

#define GPUCTL_UNSET INT32_MIN  /* sentinel for an unset/"None" optional int field */
#define GPUCTL_MAX_PSTATES 16
#define GPUCTL_MAX_VF_POINTS 32
#define GPUCTL_MAX_FAN_POINTS 8
#define GPUCTL_MAX_GPUS 8
#define GPUCTL_MAX_PROFILE_MODES 8

typedef enum {
    GPUCTL_CMD_PING = 1,
    GPUCTL_CMD_LIST_GPUS = 10,
    GPUCTL_CMD_GET_CAPS = 11,
    GPUCTL_CMD_GET_CONFIG = 12,
    GPUCTL_CMD_SET_CONFIG = 13,   /* applies immediately, arms the revert timer */
    GPUCTL_CMD_CONFIRM = 14,      /* cancels the timer, persists to /etc/vitals/gpud.conf */
    GPUCTL_CMD_REVERT = 15,       /* immediate manual revert to the last-confirmed config */
    GPUCTL_CMD_RESET_DEFAULT = 16,
    GPUCTL_CMD_SHUTDOWN = 17     /* clean exit(0); systemd's Restart=on-failure won't bring it back */
} GpuCtlCommand;

typedef enum {
    GPUCTL_VENDOR_AMD = 0,
    GPUCTL_VENDOR_NVIDIA = 1
} GpuCtlVendor;

typedef enum {
    GPUCTL_FAN_MODE_CURVE = 0,
    GPUCTL_FAN_MODE_STATIC = 1
} GpuCtlFanMode;

typedef enum {
    GPUCTL_PERF_AUTO = 0,
    GPUCTL_PERF_LOW = 1,
    GPUCTL_PERF_HIGH = 2,
    GPUCTL_PERF_MANUAL = 3
} GpuCtlPerformanceLevel;

typedef struct { int32_t clockspeedMhz, voltageMv; } GpuCtlVfPointPOD;          /* AMD GCN/RDNA1 gpu_vf_curve / mem_vf_curve */
typedef struct { int32_t freqOffsetKhz; } GpuCtlNvidiaVfPointPOD;               /* NvAPI clk-vf-point control: offset only, no absolute voltage write */
typedef struct { float tempC, speed01; } GpuCtlFanPointPOD;
typedef struct { uint32_t pstate; int32_t offsetMhz; } GpuCtlClockOffsetPOD;
typedef struct { uint8_t point; GpuCtlVfPointPOD v; } GpuCtlVfCurvePointPOD;
typedef struct { uint8_t point; GpuCtlNvidiaVfPointPOD v; } GpuCtlNvidiaVfCurvePointPOD;

typedef struct {
    bool enabled;
    int32_t mode;                 /* GpuCtlFanMode; GPUCTL_UNSET = leave the fan alone entirely */
    float staticSpeed01;
    char temperatureKey[16];      /* "edge"/"junction"/"mem" */
    uint32_t intervalMs;
    uint32_t curveCount;
    GpuCtlFanPointPOD curve[GPUCTL_MAX_FAN_POINTS];  /* RDNA3+ requires exactly 5 when set — daemon validates */
    int32_t spindownDelayMs, changeThresholdC, autoThresholdC; /* GPUCTL_UNSET = None */
} GpuCtlFanConfigPOD;

typedef struct {                                                          /* AMD RDNA3+ PMFW options */
    bool set;
    int32_t acousticLimitRpm, acousticTargetRpm, minimumPwmPct, targetTempC; /* GPUCTL_UNSET = None */
    bool zeroRpmSet, zeroRpm;
    int32_t zeroRpmThresholdC;
} GpuCtlPmfwPOD;

typedef struct { bool set; int32_t targetTempC; } GpuCtlNvidiaThermalPOD;

typedef struct {
    int32_t minCoreClockMhz, maxCoreClockMhz, minMemClockMhz, maxMemClockMhz;   /* GPUCTL_UNSET = None */
    int32_t minVoltageMv, maxVoltageMv, voltageOffsetMv;                       /* AMD */
    int32_t voltageBoostPct;                                                    /* Nvidia: 0-100, NvAPI client_volt_rails percent_delta */
    uint32_t gpuClockOffsetCount; GpuCtlClockOffsetPOD gpuClockOffsets[GPUCTL_MAX_PSTATES];
    uint32_t memClockOffsetCount; GpuCtlClockOffsetPOD memClockOffsets[GPUCTL_MAX_PSTATES];
    uint32_t gpuVfCurveCount; GpuCtlVfCurvePointPOD gpuVfCurve[GPUCTL_MAX_VF_POINTS];   /* AMD GCN/RDNA1 */
    uint32_t memVfCurveCount; GpuCtlVfCurvePointPOD memVfCurve[GPUCTL_MAX_VF_POINTS];   /* AMD GCN */
    uint32_t nvidiaVfCurveCount; GpuCtlNvidiaVfCurvePointPOD nvidiaVfCurve[GPUCTL_MAX_VF_POINTS]; /* experimental, see nvidia_nvapi_shim */
} GpuCtlClocksPOD;

typedef struct {
    int32_t powerCapWatts;              /* GPUCTL_UNSET = None */
    int32_t performanceLevel;           /* GpuCtlPerformanceLevel; -1 unset */
    int32_t powerMizerMode;             /* Nvidia PowerMizer enum; -1 unset */
    int32_t powerProfileModeIndex;      /* AMD; -1 unset, requires performanceLevel == manual */
    uint32_t corePstatesEnabledMask, memPstatesEnabledMask; /* bit i = pstate i enabled */
    GpuCtlFanConfigPOD fan;
    GpuCtlPmfwPOD pmfw;
    GpuCtlNvidiaThermalPOD nvidiaThermal;
    GpuCtlClocksPOD clocks;
} GpuCtlConfigPOD;

typedef struct {                                                          /* driver-advertised bounds, for client-side slider ranges */
    int32_t vendor;                 /* GpuCtlVendor */
    char pciSlot[16], name[128];
    int32_t powerCapMinW, powerCapMaxW, powerCapDefaultW;
    int32_t coreClockMinMhz, coreClockMaxMhz, memClockMinMhz, memClockMaxMhz; /* legal OD_RANGE bounds — slider min/max */
    /* AMD only: today's actual clocks from OD_SCLK/OD_MCLK's "0:"/"1:" lines
     * (distinct from the OD_RANGE bounds above, which are just how far a
     * slider is allowed to move) — 0 if unavailable, callers should fall
     * back to the OD_RANGE bound of the same name in that case. */
    int32_t coreClockDefaultMinMhz, coreClockDefaultMaxMhz, memClockDefaultMinMhz, memClockDefaultMaxMhz;
    int32_t voltageOffsetMinMv, voltageOffsetMaxMv;
    uint32_t corePstateCount, memPstateCount;
    uint32_t profileModeCount; char profileModeNames[GPUCTL_MAX_PROFILE_MODES][32];
    bool supportsVfCurve;            /* AMD GCN/RDNA1 only */
    bool supportsNvidiaVfCurve;      /* Nvidia NvAPI shim available (libnvidia-api.so.1 loaded) */
} GpuCtlCapsPOD;

/* Wire pattern (identical to ram_oc_shm.h): int32_t command code as raw
 * bytes, then (for commands that need one) a fixed-size POD request; the
 * daemon replies with the matching fixed-size POD response. One connection
 * per call — see gpu_ctl_daemon/main.cpp's HandleClient / src/gpu_ctl/gpu_ctl_ipc.cpp. */

typedef struct { int32_t gpuIndex; } GpuCtlGpuIndexRequestPOD; /* GET_CAPS / GET_CONFIG / REVERT / RESET_DEFAULT */
typedef struct { int32_t gpuIndex; GpuCtlConfigPOD config; } GpuCtlSetConfigRequestPOD;
/* CONFIRM carries the same config back so the daemon doesn't need to keep
 * (and re-serve) a separate "config currently applied but not yet confirmed"
 * slot — the client always confirms with the exact config it just SET. */
typedef struct { int32_t gpuIndex; GpuCtlConfigPOD config; } GpuCtlConfirmRequestPOD;

typedef struct { uint8_t ok; char error[128]; uint32_t gpuCount; GpuCtlCapsPOD gpus[GPUCTL_MAX_GPUS]; } GpuCtlListGpusResponsePOD;
typedef struct { uint8_t ok; char error[128]; GpuCtlCapsPOD caps; } GpuCtlGetCapsResponsePOD;
typedef struct { uint8_t ok; char error[128]; GpuCtlConfigPOD config; } GpuCtlGetConfigResponsePOD;
typedef struct { uint8_t ok; char error[128]; uint32_t revertDeadlineSec; } GpuCtlSetConfigResponsePOD;
typedef struct { uint8_t ok; char error[128]; } GpuCtlStatusResponsePOD;

/* "No change" config: every optional field unset. Shared by the daemon's
 * GET_CONFIG fallback and the UI so neither ever sends a zeroed POD (0 MHz
 * max clock would be clamped to the OD_RANGE floor and actually applied). */
static inline void gpuctl_config_init_unset(GpuCtlConfigPOD* c) {
    memset(c, 0, sizeof(*c));
    c->powerCapWatts = GPUCTL_UNSET;
    c->performanceLevel = -1;
    c->powerMizerMode = -1;
    c->powerProfileModeIndex = -1;
    c->fan.mode = GPUCTL_UNSET; /* without this every apply would reset the fan to auto */
    c->fan.spindownDelayMs = c->fan.changeThresholdC = c->fan.autoThresholdC = GPUCTL_UNSET;
    c->pmfw.acousticLimitRpm = c->pmfw.acousticTargetRpm = GPUCTL_UNSET;
    c->pmfw.minimumPwmPct = c->pmfw.targetTempC = c->pmfw.zeroRpmThresholdC = GPUCTL_UNSET;
    c->clocks.minCoreClockMhz = c->clocks.maxCoreClockMhz = GPUCTL_UNSET;
    c->clocks.minMemClockMhz = c->clocks.maxMemClockMhz = GPUCTL_UNSET;
    c->clocks.minVoltageMv = c->clocks.maxVoltageMv = GPUCTL_UNSET;
    c->clocks.voltageOffsetMv = GPUCTL_UNSET;
    c->clocks.voltageBoostPct = GPUCTL_UNSET;
}

/* Trust-boundary clamp for any config that arrived over the socket or from
 * disk: every count indexes a fixed array in the (root) daemon. */
#define GPUCTL_CLAMP_COUNT_(n, arr) do { if ((n) > sizeof(arr) / sizeof((arr)[0])) (n) = sizeof(arr) / sizeof((arr)[0]); } while (0)
static inline void gpuctl_config_sanitize(GpuCtlConfigPOD* c) {
    GPUCTL_CLAMP_COUNT_(c->fan.curveCount, c->fan.curve);
    GPUCTL_CLAMP_COUNT_(c->clocks.gpuClockOffsetCount, c->clocks.gpuClockOffsets);
    GPUCTL_CLAMP_COUNT_(c->clocks.memClockOffsetCount, c->clocks.memClockOffsets);
    GPUCTL_CLAMP_COUNT_(c->clocks.gpuVfCurveCount, c->clocks.gpuVfCurve);
    GPUCTL_CLAMP_COUNT_(c->clocks.memVfCurveCount, c->clocks.memVfCurve);
    GPUCTL_CLAMP_COUNT_(c->clocks.nvidiaVfCurveCount, c->clocks.nvidiaVfCurve);
    c->fan.temperatureKey[sizeof(c->fan.temperatureKey) - 1] = '\0';
    if (!isfinite(c->fan.staticSpeed01)) c->fan.staticSpeed01 = 0.5f;
    for (uint32_t i = 0; i < c->fan.curveCount; ++i) {
        if (!isfinite(c->fan.curve[i].tempC)) c->fan.curve[i].tempC = 0.0f;
        if (!isfinite(c->fan.curve[i].speed01)) c->fan.curve[i].speed01 = 1.0f; /* fail loud, not silent */
    }
    if (c->fan.intervalMs != 0 && c->fan.intervalMs < 100) c->fan.intervalMs = 100;
    if (c->fan.intervalMs > 5000) c->fan.intervalMs = 5000;
}

#ifdef __cplusplus
}
#endif
