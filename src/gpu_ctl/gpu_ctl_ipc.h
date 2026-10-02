#pragma once
#include "ipc/gpu_ctl_shm.h"

#include <string>

// Unprivileged client transport for gpu_ctl_daemon (see
// include/ipc/gpu_ctl_shm.h for the wire contract). Same request/response-
// over-a-Unix-socket shape as src/dram_oc/daemon_ipc.h, but deliberately
// simpler: no spawn, no shared-memory attach. gpu_ctl_daemon needs real
// root unconditionally and is never started or elevated by `vitals` (see
// scripts/install-daemons.sh) — every call here either reaches an
// already-running systemd-managed daemon or fails cleanly.
namespace GpuCtlIpc {

// True if the daemon answers PING right now.
bool DaemonReachable();

bool ListGpus(GpuCtlListGpusResponsePOD& out);
bool GetCaps(int gpuIndex, GpuCtlCapsPOD& out);
bool GetConfig(int gpuIndex, GpuCtlConfigPOD& out);

// Applies immediately on the daemon side and arms its revert timer.
// *deadlineSecOut (if non-null) receives how long the caller has to Confirm.
bool SetConfig(int gpuIndex, const GpuCtlConfigPOD& cfg, uint32_t* deadlineSecOut, std::string* errOut);
bool Confirm(int gpuIndex, const GpuCtlConfigPOD& cfg);
bool Revert(int gpuIndex);
bool ResetToDefault(int gpuIndex);

// Tells the daemon to exit(0) cleanly. Since vitals never elevates itself,
// bringing it back is the same one-time systemctl step as installing it:
// `sudo systemctl start vitals-gpud`.
bool Shutdown();

} // namespace GpuCtlIpc
