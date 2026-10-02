#include "nvml_dlopen.h"

#include <dlfcn.h>

namespace {

template <typename Fn>
bool Bind(void* handle, const char* name, Fn& out) {
    out = reinterpret_cast<Fn>(dlsym(handle, name));
    return out != nullptr;
}

} // namespace

bool NvmlApi::Load() {
    if (loaded_) return true;
    handle_ = dlopen("libnvidia-ml.so.1", RTLD_NOW | RTLD_GLOBAL);
    if (!handle_) return false;

    // Required calls: without these there's nothing this daemon can do for
    // Nvidia, so bail out entirely rather than half-initialize.
    bool ok = Bind(handle_, "nvmlInit_v2", nvmlInit_v2) &&
              Bind(handle_, "nvmlDeviceGetCount_v2", nvmlDeviceGetCount_v2) &&
              Bind(handle_, "nvmlDeviceGetHandleByIndex_v2", nvmlDeviceGetHandleByIndex_v2) &&
              Bind(handle_, "nvmlDeviceGetName", nvmlDeviceGetName) &&
              Bind(handle_, "nvmlDeviceGetPciInfo_v3", nvmlDeviceGetPciInfo_v3);
    if (!ok) { dlclose(handle_); handle_ = nullptr; return false; }

    // Optional/newer-driver calls: bound best-effort, callers must null-check
    // before use (older drivers or non-overclockable cards may lack some).
    Bind(handle_, "nvmlDeviceGetPowerManagementLimitConstraints", nvmlDeviceGetPowerManagementLimitConstraints);
    Bind(handle_, "nvmlDeviceGetPowerManagementDefaultLimit", nvmlDeviceGetPowerManagementDefaultLimit);
    Bind(handle_, "nvmlDeviceSetPowerManagementLimit", nvmlDeviceSetPowerManagementLimit);
    Bind(handle_, "nvmlDeviceGetGpcClkVfOffset", nvmlDeviceGetGpcClkVfOffset);
    Bind(handle_, "nvmlDeviceSetGpcClkVfOffset", nvmlDeviceSetGpcClkVfOffset);
    Bind(handle_, "nvmlDeviceGetMemClkVfOffset", nvmlDeviceGetMemClkVfOffset);
    Bind(handle_, "nvmlDeviceSetMemClkVfOffset", nvmlDeviceSetMemClkVfOffset);
    Bind(handle_, "nvmlDeviceGetTemperatureThreshold", nvmlDeviceGetTemperatureThreshold);
    Bind(handle_, "nvmlDeviceSetTemperatureThreshold", nvmlDeviceSetTemperatureThreshold);
    Bind(handle_, "nvmlDeviceSetFanSpeed_v2", nvmlDeviceSetFanSpeed_v2);
    Bind(handle_, "nvmlDeviceSetDefaultFanSpeed_v2", nvmlDeviceSetDefaultFanSpeed_v2);
    Bind(handle_, "nvmlDeviceGetNumFans", nvmlDeviceGetNumFans);
    Bind(handle_, "nvmlDeviceGetMinMaxClockOfPState", nvmlDeviceGetMinMaxClockOfPState);
    Bind(handle_, "nvmlErrorString", nvmlErrorString);

    if (nvmlInit_v2() != 0) { dlclose(handle_); handle_ = nullptr; return false; }
    loaded_ = true;
    return true;
}

NvmlApi& Nvml() {
    static NvmlApi api;
    static bool attempted = false;
    if (!attempted) { api.Load(); attempted = true; }
    return api;
}
