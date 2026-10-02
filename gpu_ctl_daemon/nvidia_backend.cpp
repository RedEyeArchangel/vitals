#include "nvidia_backend.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {

// Matches NVML's real nvmlPciInfo_t layout (NVML_API_VERSION 11+): fixed
// field sizes/order per NVIDIA's public nvml.h, reproduced here since this
// daemon dlopen's the library rather than linking against proprietary
// headers (see nvml_dlopen.h).
struct NvmlPciInfo {
    char busIdLegacy[16];
    unsigned int domain;
    unsigned int bus;
    unsigned int device;
    unsigned int pciDeviceId;
    unsigned int pciSubSystemId;
    char busId[32];
};

} // namespace

bool NvidiaDiscoverGpus(std::vector<NvidiaGpuHandle>& out) {
    NvmlApi& api = Nvml();
    if (!api.Loaded()) return false;

    unsigned int count = 0;
    if (api.nvmlDeviceGetCount_v2(&count) != 0 || count == 0) return false;

    for (unsigned int i = 0; i < count; ++i) {
        nvmlDevice_t dev = nullptr;
        if (api.nvmlDeviceGetHandleByIndex_v2(i, &dev) != 0) continue;

        NvidiaGpuHandle h;
        h.device = dev;
        h.index = i;

        char name[96] = {0};
        if (api.nvmlDeviceGetName(dev, name, sizeof(name)) == 0) h.name = name;
        else h.name = "Nvidia GPU";

        NvmlPciInfo pci{};
        if (api.nvmlDeviceGetPciInfo_v3(dev, &pci) == 0) {
            h.pciBus = pci.bus;
            // Not pci.busId: NVML pads the domain to 8 digits ("00000000:01:00.0",
            // 16 chars), which GpuCtlCapsPOD::pciSlot[16] would truncate.
            char slot[32];
            std::snprintf(slot, sizeof(slot), "%04x:%02x:%02x.0", pci.domain & 0xffff, pci.bus, pci.device);
            h.pciSlot = slot;
        }
        out.push_back(h);
    }
    return !out.empty();
}

bool NvidiaGetCaps(const NvidiaGpuHandle& gpu, GpuCtlCapsPOD* out) {
    std::memset(out, 0, sizeof(*out));
    out->vendor = GPUCTL_VENDOR_NVIDIA;
    std::snprintf(out->pciSlot, sizeof(out->pciSlot), "%s", gpu.pciSlot.c_str());
    std::snprintf(out->name, sizeof(out->name), "%s", gpu.name.c_str());

    NvmlApi& api = Nvml();
    if (api.nvmlDeviceGetPowerManagementLimitConstraints) {
        unsigned int minMw = 0, maxMw = 0;
        if (api.nvmlDeviceGetPowerManagementLimitConstraints(gpu.device, &minMw, &maxMw) == 0) {
            out->powerCapMinW = (int32_t)(minMw / 1000);
            out->powerCapMaxW = (int32_t)(maxMw / 1000);
        }
    }
    if (api.nvmlDeviceGetPowerManagementDefaultLimit) {
        unsigned int defMw = 0;
        if (api.nvmlDeviceGetPowerManagementDefaultLimit(gpu.device, &defMw) == 0)
            out->powerCapDefaultW = (int32_t)(defMw / 1000);
    }
    // NVML has no generic "clock offset range" query; the offset is
    // unbounded from NVML's own perspective (it just adds to whatever
    // pstate clock is active) — bound the UI to a conservative window
    // matching what GeForce/consumer cards typically tolerate.
    out->coreClockMinMhz = -300; out->coreClockMaxMhz = 300;
    out->memClockMinMhz = -1500; out->memClockMaxMhz = 1500;
    return true;
}

bool NvidiaApplyConfig(const NvidiaGpuHandle& gpu, const GpuCtlConfigPOD& cfg, std::string* err) {
    NvmlApi& api = Nvml();

    // NVML has no fan-curve concept and this backend has no software curve
    // loop (AMD's lives in amd_backend.cpp) — refuse instead of silently
    // applying staticSpeed01 (often 0%) as a fixed speed.
    if (cfg.fan.enabled && cfg.fan.mode == GPUCTL_FAN_MODE_CURVE) {
        if (err) *err = "fan curve mode is not supported on Nvidia (use Static or Auto)";
        return false;
    }

    GpuCtlCapsPOD caps;
    NvidiaGetCaps(gpu, &caps);
    if (cfg.powerCapWatts != GPUCTL_UNSET && api.nvmlDeviceSetPowerManagementLimit && caps.powerCapMaxW > 0) {
        int32_t w = std::clamp(cfg.powerCapWatts, caps.powerCapMinW, caps.powerCapMaxW);
        if (api.nvmlDeviceSetPowerManagementLimit(gpu.device, (unsigned int)w * 1000) != 0) {
            if (err) *err = "nvmlDeviceSetPowerManagementLimit failed";
            return false;
        }
    }

    if (cfg.clocks.gpuClockOffsetCount > 0 && api.nvmlDeviceSetGpcClkVfOffset) {
        // Nvidia exposes one offset for the whole GPC domain, not per-pstate
        // — apply the first entry's offset (matches LACT's own Nvidia model,
        // which likewise treats pstate 0 as "the" offset for consumer cards).
        api.nvmlDeviceSetGpcClkVfOffset(gpu.device, cfg.clocks.gpuClockOffsets[0].offsetMhz);
    }
    if (cfg.clocks.memClockOffsetCount > 0 && api.nvmlDeviceSetMemClkVfOffset) {
        api.nvmlDeviceSetMemClkVfOffset(gpu.device, cfg.clocks.memClockOffsets[0].offsetMhz);
    }

    if (cfg.nvidiaThermal.set && api.nvmlDeviceSetTemperatureThreshold) {
        int t = cfg.nvidiaThermal.targetTempC;
        // NVML_TEMPERATURE_THRESHOLD_ACOUSTIC_CURR = 5 per nvml.h — the only
        // settable threshold ("GPU target temperature"), same one LACT uses.
        // (6 is ACOUSTIC_MAX, a read-only bound.)
        api.nvmlDeviceSetTemperatureThreshold(gpu.device, 5, &t);
    }

    if (cfg.fan.mode == GPUCTL_UNSET) {
        // fan untouched by this config
    } else if (cfg.fan.enabled && api.nvmlDeviceSetFanSpeed_v2 && api.nvmlDeviceGetNumFans) {
        unsigned int numFans = 0;
        api.nvmlDeviceGetNumFans(gpu.device, &numFans);
        unsigned int pct = (unsigned int)(std::clamp(cfg.fan.staticSpeed01, 0.0f, 1.0f) * 100.0f);
        for (unsigned int i = 0; i < numFans; ++i) {
            // NVML_ERROR_NOT_SUPPORTED is common on cards without manual fan
            // control (most laptop/blower cards) — ignored per-fan, not fatal.
            api.nvmlDeviceSetFanSpeed_v2(gpu.device, i, pct);
        }
    } else if (!cfg.fan.enabled && api.nvmlDeviceSetDefaultFanSpeed_v2 && api.nvmlDeviceGetNumFans) {
        unsigned int numFans = 0;
        api.nvmlDeviceGetNumFans(gpu.device, &numFans);
        for (unsigned int i = 0; i < numFans; ++i) api.nvmlDeviceSetDefaultFanSpeed_v2(gpu.device, i);
    }

    return true;
}

void NvidiaResetToDefault(const NvidiaGpuHandle& gpu) {
    NvmlApi& api = Nvml();
    if (api.nvmlDeviceSetGpcClkVfOffset) api.nvmlDeviceSetGpcClkVfOffset(gpu.device, 0);
    if (api.nvmlDeviceSetMemClkVfOffset) api.nvmlDeviceSetMemClkVfOffset(gpu.device, 0);
    if (api.nvmlDeviceGetNumFans && api.nvmlDeviceSetDefaultFanSpeed_v2) {
        unsigned int numFans = 0;
        api.nvmlDeviceGetNumFans(gpu.device, &numFans);
        for (unsigned int i = 0; i < numFans; ++i) api.nvmlDeviceSetDefaultFanSpeed_v2(gpu.device, i);
    }
    if (api.nvmlDeviceSetPowerManagementLimit && api.nvmlDeviceGetPowerManagementDefaultLimit) {
        unsigned int defMw = 0;
        if (api.nvmlDeviceGetPowerManagementDefaultLimit(gpu.device, &defMw) == 0)
            api.nvmlDeviceSetPowerManagementLimit(gpu.device, defMw);
    }
}
