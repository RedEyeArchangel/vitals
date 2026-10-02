#pragma once
#include "ipc/gpu_ctl_shm.h"

#include <cstdint>
#include <string>

// EXPERIMENTAL. Nvidia VF-curve offset editing and voltage-rail
// (undervolt/boost) control have no NVML entry point — LACT reaches them
// through a handful of undocumented NvAPI "client" calls, resolved at
// runtime via NvAPI's own QueryInterface hash table (NvAPI has no public
// per-function symbols on Linux; every call, documented or not, is fetched
// this way). This is a direct C++ port of LACT's
// lact-daemon/src/server/gpu_controller/nvidia/nvapi.rs — same magic query
// IDs, same struct layouts (repr(C) in Rust == standard C struct layout on
// the same ABI, so the ports below are binary-compatible, not just
// logically equivalent).
//
// Treat this as the least trustworthy code in gpu_ctl_daemon: it depends on
// undocumented driver internals LACT itself only reverse-engineered
// empirically, and it can silently do the wrong thing on a driver version
// neither LACT nor this port has been run against. The UI must label
// anything routed through here "experimental" (see gpu.cpp's Nvidia VF
// curve section) and the daemon must never let a failure here abort
// applying the rest of a config (see nvidia_backend caller).

struct NvApiGpuHandle; // opaque, points into libnvidia-api.so.1's own handle space

class NvApiShim {
public:
    // dlopen's libnvidia-api.so.1 and calls NVAPI_INITIALIZE. Returns false
    // (leaving the shim inert) if the library or the entry point isn't
    // found — a perfectly normal outcome on AMD-only systems or older/newer
    // driver builds that don't ship this specific undocumented interface.
    bool Load();
    bool Loaded() const { return loaded_; }

    // Finds the NvAPI physical-GPU handle whose PCI bus number matches
    // `pciBus` (from NvidiaGpuHandle::pciBus, see nvidia_backend.h) — NvAPI
    // enumerates GPUs independently of NVML, so handles must be correlated
    // by bus id, not assumed to share an index.
    NvApiGpuHandle* FindMatchingGpu(uint32_t pciBus);

    // Undervolt/overvolt as a percent delta of available range (0-100),
    // matching CONFIG.md's `voltage_boost` field and NVML's absence of any
    // equivalent control.
    bool SetVoltageBoostPercent(NvApiGpuHandle* gpu, uint8_t percent, std::string* err);

    // Applies frequency-offset VF-curve points (kHz), one NvAPI call for
    // the whole batch — `points`/`count` come straight from
    // GpuCtlClocksPOD::nvidiaVfCurve.
    bool SetVfCurveOffsets(NvApiGpuHandle* gpu, const GpuCtlNvidiaVfCurvePointPOD* points, uint32_t count, std::string* err);

private:
    void* lib_ = nullptr;
    bool loaded_ = false;
};

// One process-wide instance, initialized lazily on first use.
NvApiShim& NvApi();
