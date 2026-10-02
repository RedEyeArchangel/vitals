#pragma once
#include "ipc/gpu_ctl_shm.h"

// App-facing GPU-control model. The PODs from gpu_ctl_shm.h are already
// small, flat, and ImGui-friendly (see src/pages/performance/gpu/gpu.cpp's
// "GPU Controls" section), so unlike dram_oc.h this doesn't re-shape them
// into a separate std::string/std::vector model — it just tracks daemon
// reachability + per-GPU caps, refreshed on the same throttled cadence as
// the rest of Metrics (see metrics.cpp's RefreshGpuCtl call site). Live
// config (GetConfig/SetConfig/Confirm/Revert) is called directly from the
// GPU Controls widgets when the user interacts with them, not cached here —
// same reasoning dram_oc.h documents for RunRemoteBenchmark being a
// pull-on-demand call rather than part of the polled snapshot.
struct GpuCtlState {
    bool daemonReachable = false;
    uint32_t gpuCount = 0;
    GpuCtlCapsPOD gpus[GPUCTL_MAX_GPUS]{};
};

// Pings the daemon and, if reachable, refreshes gpus[]/gpuCount. Cheap on
// failure (a single PING round-trip) — safe to call on Metrics' normal
// refresh cadence even when gpu_ctl_daemon was never installed.
void RefreshGpuCtl(GpuCtlState& out);
