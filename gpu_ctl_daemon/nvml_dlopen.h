#pragma once
#include <cstdint>

// dlopen("libnvidia-ml.so.1") at runtime instead of linking NVML at build
// time — avoids a hard build dependency on proprietary NVIDIA headers (see
// CMakeLists.txt: gpu_ctl_daemon has no Nvidia SDK in its include path).
// Degrades to Loaded()==false if the library isn't present, same
// "unavailable, not an error" posture src/metrics/gpu_sysfs.cpp already has
// for missing sensors.

typedef void* nvmlDevice_t;

struct NvmlApi {
    bool Load();  // idempotent; false if libnvidia-ml.so.1 isn't found or nvmlInit_v2 fails
    bool Loaded() const { return loaded_; }

    int (*nvmlInit_v2)() = nullptr;
    int (*nvmlDeviceGetCount_v2)(unsigned int*) = nullptr;
    int (*nvmlDeviceGetHandleByIndex_v2)(unsigned int, nvmlDevice_t*) = nullptr;
    int (*nvmlDeviceGetName)(nvmlDevice_t, char*, unsigned int) = nullptr;
    int (*nvmlDeviceGetPciInfo_v3)(nvmlDevice_t, void*) = nullptr; // struct defined in nvidia_backend.cpp
    int (*nvmlDeviceGetPowerManagementLimitConstraints)(nvmlDevice_t, unsigned int*, unsigned int*) = nullptr; // milliwatts
    int (*nvmlDeviceGetPowerManagementDefaultLimit)(nvmlDevice_t, unsigned int*) = nullptr;
    int (*nvmlDeviceSetPowerManagementLimit)(nvmlDevice_t, unsigned int) = nullptr;
    int (*nvmlDeviceGetGpcClkVfOffset)(nvmlDevice_t, int*) = nullptr;
    int (*nvmlDeviceSetGpcClkVfOffset)(nvmlDevice_t, int) = nullptr;
    int (*nvmlDeviceGetMemClkVfOffset)(nvmlDevice_t, int*) = nullptr;
    int (*nvmlDeviceSetMemClkVfOffset)(nvmlDevice_t, int) = nullptr;
    int (*nvmlDeviceGetTemperatureThreshold)(nvmlDevice_t, unsigned int, unsigned int*) = nullptr;
    int (*nvmlDeviceSetTemperatureThreshold)(nvmlDevice_t, unsigned int, int*) = nullptr;
    int (*nvmlDeviceSetFanSpeed_v2)(nvmlDevice_t, unsigned int, unsigned int) = nullptr;
    int (*nvmlDeviceSetDefaultFanSpeed_v2)(nvmlDevice_t, unsigned int) = nullptr;
    int (*nvmlDeviceGetNumFans)(nvmlDevice_t, unsigned int*) = nullptr;
    int (*nvmlDeviceGetMinMaxClockOfPState)(nvmlDevice_t, int, int, unsigned int*, unsigned int*) = nullptr;
    const char* (*nvmlErrorString)(int) = nullptr;

private:
    void* handle_ = nullptr;
    bool loaded_ = false;
};

// One process-wide instance, initialized lazily on first use.
NvmlApi& Nvml();
