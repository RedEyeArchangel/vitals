#pragma once
#include "ipc/gpu_ctl_shm.h"
#include "nvml_dlopen.h"

#include <string>
#include <vector>

// Nvidia write backend: power cap / clock offsets / fan speed / temp
// threshold via NVML (dlopen'd, see nvml_dlopen.h). VF-curve/undervolt
// (voltage_boost, nvidia_gpu_vf_curve) go through nvidia_nvapi_shim.h
// instead — NVML has no interface for those.

struct NvidiaGpuHandle {
    nvmlDevice_t device = nullptr;
    unsigned int index = 0;
    unsigned int pciBus = 0;   // for matching against NvAPI's bus-id enumeration
    std::string pciSlot;       // "0000:01:00.0"
    std::string name;
};

bool NvidiaDiscoverGpus(std::vector<NvidiaGpuHandle>& out);
bool NvidiaGetCaps(const NvidiaGpuHandle& gpu, GpuCtlCapsPOD* out);
bool NvidiaApplyConfig(const NvidiaGpuHandle& gpu, const GpuCtlConfigPOD& cfg, std::string* err);
void NvidiaResetToDefault(const NvidiaGpuHandle& gpu);
