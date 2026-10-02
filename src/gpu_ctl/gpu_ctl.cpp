#include "gpu_ctl/gpu_ctl.h"
#include "gpu_ctl/gpu_ctl_ipc.h"

#include <cstring>

void RefreshGpuCtl(GpuCtlState& out) {
    GpuCtlListGpusResponsePOD resp{};
    out.daemonReachable = GpuCtlIpc::ListGpus(resp);
    if (!out.daemonReachable) { out.gpuCount = 0; return; }
    out.gpuCount = resp.gpuCount < GPUCTL_MAX_GPUS ? resp.gpuCount : GPUCTL_MAX_GPUS;
    std::memcpy(out.gpus, resp.gpus, sizeof(out.gpus));
}
